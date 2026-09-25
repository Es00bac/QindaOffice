// SPDX-License-Identifier: GPL-3.0-or-later
// ConnectionsController: sync every connected provider, and online payment
// links for invoices (connections_controller.h has the contract).
#include "connections_controller.h"

#include "company_file.h"
#include "connections.h"
#include "documents.h"
#include "mercury_client.h"
#include "mercury_sync.h"
#include "stripe_client.h"
#include "stripe_posting.h"
#include "stripe_sync.h"

namespace QindaOffice::Books::Connect {

void ConnectionsController::sync(std::function<void(const SyncReport &)> done)
{
    CompanyFile *file = m_file();
    if (m_syncing || file == nullptr) {
        SyncReport busy;
        busy.fail(file == nullptr ? QStringLiteral("No company is open.")
                                  : QStringLiteral("A sync is already running."));
        return done(busy);
    }
    QStringList providers;
    for (const ConnectionRecord &r : Connections(*file).all()) {
        providers << r.provider;
    }
    // Mercury before Stripe; payout matching runs after each, so the order
    // only decides which sync does the matching.
    std::sort(providers.begin(), providers.end());
    if (providers.isEmpty()) {
        SyncReport none;
        none.fail(QStringLiteral("Nothing is connected yet. Open Banking ▸ Connections to connect Mercury or Stripe."));
        return done(none);
    }
    m_syncing = true;
    emit stateChanged();
    syncProviders(providers, std::make_shared<SyncReport>(), std::move(done));
}

void ConnectionsController::syncProviders(QStringList providers, std::shared_ptr<SyncReport> total,
                                          std::function<void(const SyncReport &)> done)
{
    if (providers.isEmpty()) {
        m_syncing = false;
        emit stateChanged();
        return done(*total);
    }
    const QString provider = providers.takeFirst();
    const QPointer<ConnectionsController> self(this);
    readSecret(provider, [self, provider, providers, total, done](const SecretOutcome &secret) {
        if (!self) {
            return;
        }
        if (!secret.ok()) {
            const QString name = provider == Provider::Mercury ? QStringLiteral("Mercury") : QStringLiteral("Stripe");
            total->fail(secret.status == SecretOutcome::Status::NotFound
                            ? QStringLiteral("%1 needs its API token again (the keyring has none for this company).")
                                  .arg(name)
                            : QStringLiteral("%1: %2").arg(name, secret.error));
            return self->syncProviders(providers, total, done);
        }
        const auto next = [self, providers, total, done](QObject *owner, const SyncReport &report) {
            owner->deleteLater();
            total->merge(report);
            if (self) {
                self->syncProviders(providers, total, done);
            }
        };
        const QDate today = self->today();
        if (provider == Provider::Mercury) {
            auto *client = new MercuryClient(self->m_network, self->m_mercuryBase, secret.value, self);
            auto *run = new MercurySync(client, self->m_file, today, client);
            run->run([client, next](const SyncReport &report) { next(client, report); });
        } else {
            auto *client = new StripeClient(self->m_network, self->m_stripeBase, secret.value, self);
            auto *run = new StripeSync(client, self->m_file, today, client);
            run->run([client, next](const SyncReport &report) { next(client, report); });
        }
    });
}

void ConnectionsController::createPaymentLink(qint64 invoiceId, std::function<void(const PaymentLinkOutcome &)> done)
{
    const auto fail = [done](const QString &error) { done(PaymentLinkOutcome{false, {}, error, false}); };
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return fail(QStringLiteral("No company is open."));
    }
    Connections connections(*file);
    const std::optional<ConnectionRecord> stripe = connections.get(Provider::Stripe);
    if (!stripe) {
        return fail(QStringLiteral("Connect Stripe first (Banking ▸ Connections)."));
    }
    Documents documents(*file);
    const std::optional<BusinessDocument> invoice = documents.load(invoiceId);
    const Money open = documents.openBalance(invoiceId);
    if (!invoice || invoice->kind != DocKind::Invoice || invoice->state != DocState::Open || !open.isPositive()) {
        return fail(QStringLiteral("Only an open invoice with a balance due can get a payment link."));
    }
    if (const std::optional<PaymentLinkRecord> current = connections.currentPaymentLink(invoiceId)) {
        return done(PaymentLinkOutcome{true, current->url, {}, true});
    }
    const QString uuid = connections.companyUuid();
    const QString currency = file->currency().toLower();
    const QString number = invoice->number;
    const QString product = QStringLiteral("Invoice %1 — %2").arg(number, file->settings().displayName());
    // AGENT-NOTE: one Idempotency-Key per invoice + amount (+ step): a
    // retried request after a lost answer returns the SAME price and link
    // instead of a second one (Stripe keeps keys at least 24 hours; after
    // that the stored URL above is what prevents duplicates).
    const QString idempotency =
        QStringLiteral("qindabooks-%1-inv%2-%3%4").arg(uuid).arg(invoiceId).arg(open.minor()).arg(currency);
    const QList<QPair<QString, QString>> metadata{{StripePosting::kInvoiceKey, QString::number(invoiceId)},
                                                  {StripePosting::kCompanyKey, uuid},
                                                  {StripePosting::kNumberKey, number}};
    QList<QPair<QString, QString>> fields;
    for (const auto &[key, value] : metadata) {
        fields.append({QStringLiteral("metadata[%1]").arg(key), value});
        fields.append({QStringLiteral("payment_intent_data[metadata][%1]").arg(key), value});
    }
    fields.append({QStringLiteral("payment_intent_data[description]"), QStringLiteral("Invoice %1").arg(number)});
    // One completed payment per link: a paid invoice cannot be paid twice.
    fields.append({QStringLiteral("restrictions[completed_sessions][limit]"), QStringLiteral("1")});
    const qint64 amount = open.minor();
    const QPointer<ConnectionsController> self(this);
    readSecret(Provider::Stripe, [self, fail, done, currency, amount, product, idempotency, fields,
                                  invoiceId](const SecretOutcome &secret) {
        if (!self) {
            return;
        }
        if (!secret.ok()) {
            return fail(secret.status == SecretOutcome::Status::NotFound
                            ? QStringLiteral("Stripe needs its API key again (Banking ▸ Connections).")
                            : secret.error);
        }
        auto *client = new StripeClient(self->m_network, self->m_stripeBase, secret.value, self);
        client->createPrice(currency, amount, product, idempotency + QStringLiteral("-price"),
                            [self, client, fail, done, currency, amount, idempotency, fields,
                             invoiceId](const ApiResponse &r, const QString &priceId) {
            if (!r.ok() || priceId.isEmpty()) {
                client->deleteLater();
                return fail(r.ok() ? QStringLiteral("Stripe did not return a price.") : r.error);
            }
            client->createPaymentLink(
                priceId, fields, idempotency + QStringLiteral("-link"),
                [self, client, fail, done, currency, amount, invoiceId](const ApiResponse &link, const QString &id,
                                                                        const QString &url) {
                    client->deleteLater();
                    if (!link.ok() || url.isEmpty()) {
                        return fail(link.ok() ? QStringLiteral("Stripe did not return a link.") : link.error);
                    }
                    CompanyFile *file = self ? self->m_file() : nullptr;
                    if (file == nullptr) {
                        return fail(QStringLiteral("The company was closed."));
                    }
                    PaymentLinkRecord record;
                    record.docId = invoiceId;
                    record.provider = Provider::Stripe;
                    record.amount = amount;
                    record.currency = currency.toUpper();
                    record.url = url;
                    record.remoteId = id;
                    const Result stored = Connections(*file).recordPaymentLink(record);
                    if (!stored.ok) {
                        return fail(stored.error);
                    }
                    done(PaymentLinkOutcome{true, url, {}, false});
                });
        });
    });
}

} // namespace QindaOffice::Books::Connect
