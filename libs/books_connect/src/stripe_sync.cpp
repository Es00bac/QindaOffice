// SPDX-License-Identifier: GPL-3.0-or-later
#include "stripe_sync.h"

#include "accounts.h"
#include "company_file.h"
#include "connections.h"
#include "payout_matching.h"
#include "stripe_posting.h"

#include <QTimeZone>

#include <algorithm>

namespace QindaOffice::Books::Connect {

StripeSync::StripeSync(StripeClient *client, FileProvider file, QDate today, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_file(std::move(file))
    , m_today(today)
{
}

void StripeSync::run(std::function<void(const SyncReport &)> done)
{
    m_done = std::move(done);
    CompanyFile *file = m_file();
    const std::optional<ConnectionRecord> record = file ? Connections(*file).get(Provider::Stripe) : std::nullopt;
    if (!record) {
        m_report.fail(QStringLiteral("Stripe is not connected."));
        return finish();
    }
    const StripeSettings settings = StripeSettings::fromJson(record->settings);
    if (!ChartOfAccounts(*file).account(settings.clearingAccountId)) {
        m_report.fail(QStringLiteral("Choose the Stripe clearing account in Connections first."));
        return finish();
    }
    m_cursor = record->cursor;
    const qint64 last = m_cursor.value(QStringLiteral("lastCreated")).toInteger();
    if (last > 0) {
        m_createdGte = last;
    } else {
        const QDate start = settings.startDate.isValid() ? settings.startDate : m_today.addDays(-kDefaultHistoryDays);
        m_createdGte = QDateTime(start, QTime(0, 0), QTimeZone::systemTimeZone()).toSecsSinceEpoch();
    }
    fetch(QString());
}

void StripeSync::fetch(const QString &startingAfter)
{
    if (!m_client) {
        m_report.fail(QStringLiteral("The Stripe sync was cancelled."));
        return finish();
    }
    m_client->balanceTransactions(m_createdGte, startingAfter, kPageSize,
                                  [this](const ApiResponse &r, const StripePage &page) {
                                      if (!r.ok()) {
                                          // Nothing is posted from a partial history: the
                                          // next sync starts from the same cursor.
                                          m_report.fail(QStringLiteral("Stripe: %1").arg(r.error));
                                          return finish();
                                      }
                                      m_collected += page.data;
                                      if (page.hasMore && !page.data.isEmpty()) {
                                          return fetch(page.data.last().id);
                                      }
                                      post();
                                      finish();
                                  });
}

void StripeSync::post()
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        m_report.fail(QStringLiteral("The company was closed during the sync."));
        return;
    }
    Connections connections(*file);
    const std::optional<ConnectionRecord> record = connections.get(Provider::Stripe);
    if (!record) {
        return; // disconnected while the history was downloading
    }
    // Oldest first, so a refund follows its charge and the running
    // balance reads like Stripe's.
    std::stable_sort(m_collected.begin(), m_collected.end(),
                     [](const StripeBalanceTransaction &a, const StripeBalanceTransaction &b) {
                         return a.created != b.created ? a.created < b.created : a.id < b.id;
                     });
    StripePosting posting(*file, StripeSettings::fromJson(record->settings), connections.companyUuid());
    for (const StripeBalanceTransaction &t : std::as_const(m_collected)) {
        posting.post(t, &m_report);
        if (t.created >= m_cursor.value(QStringLiteral("lastCreated")).toInteger()) {
            m_cursor.insert(QStringLiteral("lastCreated"), t.created);
            m_cursor.insert(QStringLiteral("lastId"), t.id);
        }
    }
    m_report.matched += matchStripePayouts(*file);
}

void StripeSync::finish()
{
    if (CompanyFile *file = m_file()) {
        if (Connections(*file).get(Provider::Stripe)) {
            // Notes about single payments (another company's invoice, a
            // foreign currency) are reported but do not mark the
            // connection broken; only a failed download does.
            Connections(*file).recordSync(Provider::Stripe, m_cursor, QDateTime::currentDateTimeUtc(),
                                          m_report.ok ? QString() : m_report.errors.join(QLatin1Char('\n')));
        }
    }
    if (m_done) {
        const auto done = std::move(m_done);
        m_done = nullptr;
        done(m_report);
    }
}

} // namespace QindaOffice::Books::Connect
