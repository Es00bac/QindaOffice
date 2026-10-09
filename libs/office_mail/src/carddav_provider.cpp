// SPDX-License-Identifier: GPL-3.0-or-later
// CardDAV address books of one account (carddav_provider.h).

#include "carddav_provider.h"

#include "carddav_jobs.h"
#include "contact_store.h"

#include <qindatk/protocol_client.h>

#include <QTimer>
#include <QUrl>

#include <memory>

namespace QindaOffice::Mail {

namespace {

QVariantMap outcome(const CardDav::Outcome &o)
{
    return {{QStringLiteral("ok"), o.state == QLatin1String("accepted")}, {QStringLiteral("state"), o.state},
            {QStringLiteral("error"), o.error}, {QStringLiteral("retryable"), o.retryable},
            {QStringLiteral("authRejected"), o.authRejected}, {QStringLiteral("transcript"), o.transcript},
            {QStringLiteral("requests"), o.requests}};
}

} // namespace

CardDavProvider::CardDavProvider(QObject *parent)
    : QObject(parent)
{
}

CardDavProvider::~CardDavProvider() = default;

void CardDavProvider::setStore(ContactStore *store)
{
    if (store == m_store) return;
    m_store = store;
    emit configChanged();
}

void CardDavProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism == QLatin1String("bearer") ? QStringLiteral("bearer") : QStringLiteral("basic");
    emit secretChanged();
}

void CardDavProvider::clearSecret()
{
    m_secret.clear();
    emit secretChanged();
}

CardDav::Credentials CardDavProvider::credentials(const QString &site) const
{
    CardDav::Credentials c;
    if (!m_secret.isEmpty()) {
        if (m_mechanism == QLatin1String("bearer")) {
            c.authorization = "Bearer " + m_secret.toUtf8();
            c.secrets = {m_secret};
        } else {
            const QByteArray basic = (m_userName + QLatin1Char(':') + m_secret).toUtf8().toBase64();
            c.authorization = "Basic " + basic;
            c.secrets = {m_secret, QString::fromLatin1(basic)};
        }
    }
    c.site = site;
    c.timeoutMs = m_timeoutMs;
    c.allowInsecureAuth = m_allowInsecureAuth;
    return c;
}

void CardDavProvider::setBusy(int delta, const QString &error)
{
    m_pending = qMax(0, m_pending + delta);
    m_lastError = error;
    emit stateChanged();
}

void CardDavProvider::rejectLater(bool discovery, const QString &requestId, const QString &error, bool authRejected)
{
    const QVariantMap result{{QStringLiteral("requestId"), requestId}, {QStringLiteral("ok"), false},
                             {QStringLiteral("state"), QStringLiteral("rejected")}, {QStringLiteral("error"), error},
                             {QStringLiteral("retryable"), false}, {QStringLiteral("authRejected"), authRejected}};
    QTimer::singleShot(0, this, [this, discovery, requestId, result] {
        m_lastError = result.value(QStringLiteral("error")).toString();
        emit stateChanged();
        if (discovery) emit discoveryFinished(requestId, result);
        else emit syncFinished(requestId, result);
    });
}

bool CardDavProvider::discover(const QString &requestId, const QString &url)
{
    const QUrl start(url.trimmed().isEmpty() ? m_url : url.trimmed());
    if (!start.isValid() || start.host().isEmpty()) {
        rejectLater(true, requestId, tr("Address book discovery needs an https address"));
        return false;
    }
    const CardDav::Credentials creds = credentials(start.host());
    auto result = std::make_shared<CardDav::Discovery>();
    setBusy(+1, QString());
    QindaTK::Protocol::startWorker(
        this, [start, creds, result] {
            *result = CardDav::discover(start, creds);
            return QVariantMap();
        },
        [this, requestId, result](const QVariantMap &) {
            QVariantMap out = outcome(*result);
            QVariantList books;
            for (const CardDav::DiscoveredBook &book : std::as_const(result->books))
                books.append(QVariantMap{{QStringLiteral("url"), book.url}, {QStringLiteral("name"), book.name}});
            out.insert(QStringLiteral("requestId"), requestId);
            out.insert(QStringLiteral("addressBooks"), books);
            out.insert(QStringLiteral("principalUrl"), result->principalUrl);
            out.insert(QStringLiteral("homeSetUrl"), result->homeSetUrl);
            setBusy(-1, result->error);
            emit discoveryFinished(requestId, out);
        });
    return true;
}

bool CardDavProvider::sync(const QString &requestId)
{
    if (m_store == nullptr) {
        rejectLater(false, requestId, tr("The address book is not available"));
        return false;
    }
    if (m_secret.isEmpty()) {
        rejectLater(false, requestId, tr("The address book account has no credentials"), true);
        return false;
    }
    const QUrl base(m_url);
    QList<CardDav::BookSync> jobs;
    QStringList books;
    QList<QHash<QString, QString>> digests;
    for (const QVariant &value : std::as_const(m_books)) {
        const QVariantMap book = value.toMap();
        const QString url = book.value(QStringLiteral("url")).toString();
        if (!book.value(QStringLiteral("enabled"), true).toBool() || url.isEmpty()) continue;
        const ContactStore::SyncSnapshot snapshot = m_store->syncSnapshot(m_accountId, url);
        CardDav::BookSync job;
        job.url = base.resolved(QUrl(url));
        job.syncToken = snapshot.syncToken;
        job.ctag = snapshot.ctag;
        QHash<QString, QString> sent;
        for (const ContactStore::SyncCard &card : snapshot.cards) {
            job.cards.append({card.contactId, card.href, card.etag, card.uid, card.vcard, card.changed});
            if (card.changed) sent.insert(card.contactId, ContactStore::syncDigest(card.vcard));
        }
        for (const auto &[href, etag] : snapshot.deleted) job.deleted.append({href, etag});
        jobs.append(job);
        books.append(url);
        digests.append(sent);
    }
    const CardDav::Credentials creds = credentials(base.host());
    auto result = std::make_shared<CardDav::SyncResult>();
    setBusy(+1, QString());
    QindaTK::Protocol::startWorker(
        this, [jobs, creds, result] {
            *result = CardDav::sync(jobs, creds);
            return QVariantMap();
        },
        [this, requestId, result, books, digests](const QVariantMap &) { finishSync(requestId, *result, books, digests); });
    return true;
}

void CardDavProvider::finishSync(const QString &requestId, const CardDav::SyncResult &result, const QStringList &books,
                                 const QList<QHash<QString, QString>> &digests)
{
    QVariantMap out = outcome(result);
    int created = 0, updated = 0, deleted = 0, pushed = 0;
    QStringList conflictNames;
    QString storeError;
    // Every book the dialogue finished is applied, also when a later one failed.
    for (qsizetype i = 0; i < result.books.size() && i < books.size(); ++i) {
        const CardDav::BookResult &r = result.books.at(i);
        ContactStore::SyncApply apply;
        apply.syncToken = r.syncToken;
        apply.ctag = r.ctag;
        for (const CardDav::RemoteCard &card : r.changed) apply.changed.append({card.href, card.etag, card.vcard});
        for (const CardDav::RemoteCard &card : r.restored) apply.restored.append({card.href, card.etag, card.vcard});
        apply.removed = r.removed;
        apply.deletedHrefs = r.deletedHrefs;
        apply.conflicts = r.conflicts;
        for (const CardDav::Pushed &p : r.pushed) apply.pushed.append({p.contactId, p.href, p.etag});
        apply.sentDigests = digests.at(i);
        const QVariantMap applied = m_store != nullptr ? m_store->applySync(m_accountId, books.at(i), apply)
                                                       : QVariantMap{{QStringLiteral("ok"), false}};
        if (!applied.value(QStringLiteral("ok")).toBool() && storeError.isEmpty())
            storeError = applied.value(QStringLiteral("error")).toString();
        created += applied.value(QStringLiteral("created")).toInt();
        updated += applied.value(QStringLiteral("updated")).toInt();
        deleted += applied.value(QStringLiteral("deleted")).toInt();
        pushed += int(r.pushed.size());
        conflictNames += applied.value(QStringLiteral("conflicts")).toStringList();
    }
    if (!storeError.isEmpty() && out.value(QStringLiteral("ok")).toBool()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("state"), QStringLiteral("rejected"));
        out.insert(QStringLiteral("error"), storeError);
    }
    out.insert(QStringLiteral("requestId"), requestId);
    out.insert(QStringLiteral("created"), created);
    out.insert(QStringLiteral("updated"), updated);
    out.insert(QStringLiteral("deleted"), deleted);
    out.insert(QStringLiteral("pushed"), pushed);
    out.insert(QStringLiteral("conflicts"), int(conflictNames.size()));
    out.insert(QStringLiteral("conflictNames"), conflictNames);
    out.insert(QStringLiteral("books"), int(result.books.size()));
    setBusy(-1, out.value(QStringLiteral("error")).toString());
    emit syncFinished(requestId, out);
}

} // namespace QindaOffice::Mail
