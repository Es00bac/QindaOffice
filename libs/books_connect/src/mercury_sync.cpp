// SPDX-License-Identifier: GPL-3.0-or-later
#include "mercury_sync.h"

#include "accounts.h"
#include "banking.h"
#include "company_file.h"
#include "connections.h"
#include "mercury_client.h"
#include "payout_matching.h"

#include <QTimeZone>

namespace QindaOffice::Books::Connect {

namespace {

QString joined(const QStringList &parts)
{
    QStringList kept;
    for (const QString &p : parts) {
        if (!p.trimmed().isEmpty() && !kept.contains(p.trimmed())) {
            kept << p.trimmed();
        }
    }
    return kept.join(QStringLiteral(" · "));
}

BankLine lineOf(const MercuryTransaction &t)
{
    BankLine line;
    line.date = t.postedAt.toTimeZone(QTimeZone::systemTimeZone()).date();
    line.amount = t.amount;
    line.payee = t.counterpartyName.isEmpty() ? t.bankDescription : t.counterpartyName;
    line.memo = joined({t.bankDescription, t.note, t.externalMemo});
    line.fitid = t.id;
    line.checkNumber = t.checkNumber;
    return line;
}

} // namespace

MercurySync::MercurySync(MercuryClient *client, FileProvider file, QDate today, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_file(std::move(file))
    , m_today(today)
{
}

void MercurySync::run(std::function<void(const SyncReport &)> done)
{
    m_done = std::move(done);
    CompanyFile *file = m_file();
    const std::optional<ConnectionRecord> record = file ? Connections(*file).get(Provider::Mercury) : std::nullopt;
    if (!record) {
        m_report.fail(QStringLiteral("Mercury is not connected."));
        return finish();
    }
    if (file->currency() != QLatin1String("USD")) {
        m_report.fail(QStringLiteral("Mercury accounts are in US dollars; these books are kept in %1.")
                          .arg(file->currency()));
        return finish();
    }
    m_cursor = record->cursor;
    const MercurySettings settings = MercurySettings::fromJson(record->settings);
    ChartOfAccounts chart(*file);
    for (auto it = settings.links.cbegin(); it != settings.links.cend(); ++it) {
        if (chart.account(it.value())) {
            m_queue << qMakePair(it.key(), it.value());
        }
    }
    std::sort(m_queue.begin(), m_queue.end());
    nextAccount();
}

void MercurySync::nextAccount()
{
    if (m_queue.isEmpty()) {
        return finish();
    }
    const auto [mercuryId, booksId] = m_queue.takeFirst();
    m_accountId = mercuryId;
    m_booksAccountId = booksId;
    m_collected.clear();
    fetch(0);
}

void MercurySync::fetch(int offset)
{
    if (!m_client) {
        m_report.fail(QStringLiteral("The Mercury sync was cancelled."));
        return finish();
    }
    CompanyFile *file = m_file();
    if (file == nullptr) {
        m_report.fail(QStringLiteral("The company was closed during the sync."));
        return finish();
    }
    const QJsonObject position = m_cursor.value(m_accountId).toObject();
    const QDateTime lastPosted =
        QDateTime::fromString(position.value(QStringLiteral("lastPostedAt")).toString(), Qt::ISODateWithMs);
    const QDate pendingSince = QDate::fromString(position.value(QStringLiteral("pendingSince")).toString(), Qt::ISODate);
    QDate start;
    if (lastPosted.isValid()) {
        start = lastPosted.toUTC().date().addDays(-7);
    } else {
        const MercurySettings settings =
            MercurySettings::fromJson(Connections(*file).get(Provider::Mercury).value_or(ConnectionRecord{}).settings);
        start = settings.startDate.isValid() ? settings.startDate : m_today.addDays(-kDefaultHistoryDays);
    }
    if (pendingSince.isValid() && pendingSince < start) {
        start = pendingSince;
    }
    m_client->transactions(m_accountId, start, offset, kPageSize,
                           [this, offset](const ApiResponse &r, const MercuryPage &page) {
                               if (!r.ok()) {
                                   m_report.fail(QStringLiteral("Mercury: %1").arg(r.error));
                                   return nextAccount();
                               }
                               m_collected += page.transactions;
                               const int next = offset + int(page.transactions.size());
                               if (!page.transactions.isEmpty() && next < page.total) {
                                   return fetch(next);
                               }
                               importCollected();
                               nextAccount();
                           });
}

void MercurySync::importCollected()
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        m_report.fail(QStringLiteral("The company was closed during the sync."));
        return;
    }
    QJsonObject position = m_cursor.value(m_accountId).toObject();
    QDateTime lastPosted =
        QDateTime::fromString(position.value(QStringLiteral("lastPostedAt")).toString(), Qt::ISODateWithMs);
    QDate pendingSince;
    QList<BankLine> lines;
    for (const MercuryTransaction &t : std::as_const(m_collected)) {
        if (t.posted()) {
            lines << lineOf(t);
            if (!lastPosted.isValid() || t.postedAt > lastPosted) {
                lastPosted = t.postedAt;
            }
        } else if (t.status == QLatin1String("pending")) {
            const QDate created = (t.createdAt.isValid() ? t.createdAt.toUTC().date() : m_today).addDays(-1);
            if (!pendingSince.isValid() || created < pendingSince) {
                pendingSince = created;
            }
        }
    }
    Banking banking(*file);
    const ImportSummary summary =
        lines.isEmpty() ? ImportSummary{} : banking.importLines(m_booksAccountId, lines, QStringLiteral("mercury"));
    m_report.imported += summary.added;
    if (summary.added > 0) {
        banking.applyRules(m_booksAccountId);
    }
    m_report.matched += matchStripePayouts(*file, m_booksAccountId);
    if (lastPosted.isValid()) {
        position.insert(QStringLiteral("lastPostedAt"), lastPosted.toUTC().toString(Qt::ISODateWithMs));
    }
    if (pendingSince.isValid()) {
        position.insert(QStringLiteral("pendingSince"), pendingSince.toString(Qt::ISODate));
    } else {
        position.remove(QStringLiteral("pendingSince"));
    }
    m_cursor.insert(m_accountId, position);
}

void MercurySync::finish()
{
    if (CompanyFile *file = m_file()) {
        if (Connections(*file).get(Provider::Mercury)) {
            Connections(*file).recordSync(Provider::Mercury, m_cursor, QDateTime::currentDateTimeUtc(),
                                          m_report.errors.join(QLatin1Char('\n')));
        }
    }
    if (m_done) {
        const auto done = std::move(m_done);
        m_done = nullptr;
        done(m_report);
    }
}

} // namespace QindaOffice::Books::Connect
