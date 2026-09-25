// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Connections: what a company file remembers about its online bank and
// payment connections (M48, D-048; docs/books.md "Bank and payment
// connections") — which provider is connected, its settings (account
// links, flags), its sync cursor, the last sync time and error — plus the
// company's stable identity (`companyUuid`) and the online payment links
// made for invoices.
//
// AGENT-GUARD (secrets): API tokens are NEVER stored here or anywhere in
// the company file. They live in the desktop keyring under
// `<companyUuid>/<provider>` (books_connect/src/connect_secrets.h). save()
// refuses settings or cursors that contain a string looking like a token
// (Mercury `secret-token:…`, Stripe `sk_…`/`rk_…`), so a caller bug
// cannot leak one into a file the user copies around.
//
// AGENT-CONTRACT (storage, schema v3): table `connections` keyed by
// provider ("mercury" | "stripe"), JSON text columns `settings` and
// `cursor`; table `payment_links`; settings key `company_uuid`. The JSON
// keys each provider uses are documented in books_connect
// (mercury_sync.h, stripe_sync.h) — add keys freely, never rename one.

#include "books_types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

namespace Provider {
inline const QString Mercury = QStringLiteral("mercury");
inline const QString Stripe = QStringLiteral("stripe");
} // namespace Provider

struct ConnectionRecord {
    QString provider;
    QString status = QStringLiteral("connected"); // connected | error
    QJsonObject settings;
    QJsonObject cursor;
    QDateTime lastSync; // UTC; invalid = never
    QString lastError;  // empty = the last sync succeeded
};

struct PaymentLinkRecord {
    qint64 id = 0;
    qint64 docId = 0;
    QString provider;
    qint64 amount = 0; // minor units the link charges
    QString currency;
    QString url;
    QString remoteId;  // e.g. Stripe plink_…
    QDateTime createdAt;
};

class Connections {
public:
    explicit Connections(CompanyFile &file);

    // The company's stable identity; created (and audited) on first use.
    // A copy of the file (Save As, backup) keeps it — it is the same
    // business, so it reaches the same keyring entries.
    [[nodiscard]] QString companyUuid();

    [[nodiscard]] std::optional<ConnectionRecord> get(const QString &provider) const;
    [[nodiscard]] QList<ConnectionRecord> all() const;
    // Inserts or replaces the provider's row. Audited (settings only; the
    // cursor is bookkeeping). Refuses an unknown provider and anything
    // that looks like a credential (see the guard above).
    Result save(const ConnectionRecord &record);
    // Records a sync outcome: cursor, time and error. Not audited (the
    // imported lines and postings are).
    Result recordSync(const QString &provider, const QJsonObject &cursor, const QDateTime &when,
                      const QString &error);
    // Forgets the provider's settings and cursor; imported history stays.
    Result remove(const QString &provider);

    // ---- payment links ----
    Result recordPaymentLink(const PaymentLinkRecord &link);
    [[nodiscard]] std::optional<PaymentLinkRecord> latestPaymentLink(qint64 docId) const;
    // The newest link whose amount equals the document's open balance
    // while that balance is positive; a link made for a different amount
    // (the invoice changed or was partly paid since) is stale and never
    // printed or mailed.
    [[nodiscard]] std::optional<PaymentLinkRecord> currentPaymentLink(qint64 docId) const;

    // True when `text` contains something shaped like an API token.
    [[nodiscard]] static bool looksLikeSecret(const QString &text);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
