// SPDX-License-Identifier: GPL-3.0-or-later
// Connections: provider rows, the company identity and payment links
// (schema v3; connections.h has the contract).
#include "connections.h"

#include "company_file.h"
#include "documents.h"
#include "sql_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUuid>

namespace QindaOffice::Books {

namespace {

QString jsonText(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject jsonObject(const QVariant &value)
{
    return QJsonDocument::fromJson(value.toString().toUtf8()).object();
}

bool containsSecret(const QJsonValue &value)
{
    if (value.isString()) {
        return Connections::looksLikeSecret(value.toString());
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (Connections::looksLikeSecret(it.key()) || containsSecret(it.value())) {
                return true;
            }
        }
    }
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) {
            if (containsSecret(v)) {
                return true;
            }
        }
    }
    return false;
}

bool knownProvider(const QString &provider)
{
    return provider == Provider::Mercury || provider == Provider::Stripe;
}

PaymentLinkRecord readLink(const QSqlQuery &q)
{
    PaymentLinkRecord r;
    r.id = q.value(0).toLongLong();
    r.docId = q.value(1).toLongLong();
    r.provider = q.value(2).toString();
    r.amount = q.value(3).toLongLong();
    r.currency = q.value(4).toString();
    r.url = q.value(5).toString();
    r.remoteId = q.value(6).toString();
    r.createdAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    return r;
}

} // namespace

Connections::Connections(CompanyFile &file)
    : m_file(file)
{
}

bool Connections::looksLikeSecret(const QString &text)
{
    // Mercury tokens carry the `secret-token:` prefix; Stripe secret and
    // restricted keys are sk_/rk_ + live/test + a long random tail.
    static const QRegularExpression pattern(
        QStringLiteral("secret-token:|\\b(?:sk|rk)_(?:live|test)_[A-Za-z0-9]{8,}"));
    return pattern.match(text).hasMatch();
}

QString Connections::companyUuid()
{
    const QString existing =
        Sql::scalar(m_file.database(), QStringLiteral("SELECT value FROM settings WHERE key = 'company_uuid'"))
            .toString();
    if (!existing.isEmpty()) {
        return existing;
    }
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    SqlTransaction txn(m_file);
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES('company_uuid', ?)"), {uuid})
        || !m_file.audit(QStringLiteral("create"), QStringLiteral("company_uuid"), 0, {},
                         QStringLiteral("{\"company_uuid\":\"%1\"}").arg(uuid))
        || !txn.commit()) {
        return QString();
    }
    return uuid;
}

std::optional<ConnectionRecord> Connections::get(const QString &provider) const
{
    for (const ConnectionRecord &r : all()) {
        if (r.provider == provider) {
            return r;
        }
    }
    return std::nullopt;
}

QList<ConnectionRecord> Connections::all() const
{
    QList<ConnectionRecord> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT provider, status, settings, cursor, last_sync, last_error FROM connections"
                               " ORDER BY provider"));
    while (q.next()) {
        ConnectionRecord r;
        r.provider = q.value(0).toString();
        r.status = q.value(1).toString();
        r.settings = jsonObject(q.value(2));
        r.cursor = jsonObject(q.value(3));
        r.lastSync = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
        r.lastError = q.value(5).toString();
        out << r;
    }
    return out;
}

Result Connections::save(const ConnectionRecord &record)
{
    if (!knownProvider(record.provider)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("unknown provider \"%1\"").arg(record.provider));
    }
    // AGENT-GUARD: see the header — never a credential in the file.
    if (containsSecret(record.settings) || containsSecret(record.cursor) || looksLikeSecret(record.lastError)) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("connection settings must not contain an API token"));
    }
    const std::optional<ConnectionRecord> before = get(record.provider);
    const QString now = CompanyFile::nowStamp();
    SqlTransaction txn(m_file);
    QString error;
    const bool written = Sql::exec(
        m_file.database(),
        QStringLiteral("INSERT INTO connections(provider, status, settings, cursor, last_sync, last_error,"
                       " created_at, updated_at) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(provider) DO UPDATE SET"
                       " status = excluded.status, settings = excluded.settings, cursor = excluded.cursor,"
                       " last_sync = excluded.last_sync, last_error = excluded.last_error,"
                       " updated_at = excluded.updated_at"),
        {record.provider, record.status, jsonText(record.settings), jsonText(record.cursor),
         record.lastSync.isValid() ? QVariant(record.lastSync.toUTC().toString(Qt::ISODateWithMs)) : QVariant(),
         record.lastError.isEmpty() ? QVariant() : QVariant(record.lastError), now, now},
        &error);
    if (!written
        || !m_file.audit(before ? QStringLiteral("update") : QStringLiteral("connect"), QStringLiteral("connection"),
                         0, before ? jsonText(before->settings) : QString(), jsonText(record.settings),
                         record.provider)
        || !txn.commit()) {
        return Result::failure(ErrorCode::Storage, error.isEmpty() ? txn.error() : error);
    }
    return Result::success();
}

Result Connections::recordSync(const QString &provider, const QJsonObject &cursor, const QDateTime &when,
                               const QString &error)
{
    if (containsSecret(cursor) || looksLikeSecret(error)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("sync state must not contain an API token"));
    }
    SqlTransaction txn(m_file);
    QString message;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE connections SET cursor = ?, last_sync = ?, last_error = ?, status = ?,"
                                  " updated_at = ? WHERE provider = ?"),
                   {jsonText(cursor), when.toUTC().toString(Qt::ISODateWithMs),
                    error.isEmpty() ? QVariant() : QVariant(error),
                    error.isEmpty() ? QStringLiteral("connected") : QStringLiteral("error"), CompanyFile::nowStamp(),
                    provider},
                   &message)
        || !txn.commit()) {
        return Result::failure(ErrorCode::Storage, message.isEmpty() ? txn.error() : message);
    }
    return Result::success();
}

Result Connections::remove(const QString &provider)
{
    const std::optional<ConnectionRecord> before = get(provider);
    if (!before) {
        return Result::success();
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("DELETE FROM connections WHERE provider = ?"), {provider}, &error)
        || !m_file.audit(QStringLiteral("disconnect"), QStringLiteral("connection"), 0, jsonText(before->settings), {},
                         provider)
        || !txn.commit()) {
        return Result::failure(ErrorCode::Storage, error.isEmpty() ? txn.error() : error);
    }
    return Result::success();
}

Result Connections::recordPaymentLink(const PaymentLinkRecord &link)
{
    if (link.docId == 0 || link.url.isEmpty() || !knownProvider(link.provider)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a payment link needs a document and a URL"));
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO payment_links(doc_id, provider, amount, currency, url, remote_id, created_at)"
                       " VALUES(?,?,?,?,?,?,?)"),
        {link.docId, link.provider, link.amount, link.currency, link.url, link.remoteId, CompanyFile::nowStamp()},
        &error);
    if (id == 0
        || !m_file.audit(QStringLiteral("payment_link"), QStringLiteral("document"), link.docId, {},
                         jsonText(QJsonObject{{QStringLiteral("provider"), link.provider},
                                              {QStringLiteral("amount"), link.amount},
                                              {QStringLiteral("url"), link.url}}))
        || !txn.commit()) {
        return Result::failure(ErrorCode::Storage, error.isEmpty() ? txn.error() : error);
    }
    return Result::success(id);
}

std::optional<PaymentLinkRecord> Connections::latestPaymentLink(qint64 docId) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, doc_id, provider, amount, currency, url, remote_id, created_at"
                               " FROM payment_links WHERE doc_id = ? ORDER BY id DESC LIMIT 1"),
             {docId});
    if (!q.next()) {
        return std::nullopt;
    }
    return readLink(q);
}

std::optional<PaymentLinkRecord> Connections::currentPaymentLink(qint64 docId) const
{
    const std::optional<PaymentLinkRecord> link = latestPaymentLink(docId);
    if (!link) {
        return std::nullopt;
    }
    Documents documents(m_file);
    const std::optional<BusinessDocument> doc = documents.load(docId);
    const Money open = documents.openBalance(docId);
    if (!doc || doc->state != DocState::Open || !open.isPositive() || open.minor() != link->amount
        || link->currency != m_file.currency()) {
        return std::nullopt;
    }
    return link;
}

} // namespace QindaOffice::Books
