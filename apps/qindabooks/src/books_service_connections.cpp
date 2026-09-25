// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: bank and payment connections — Mercury and Stripe (M48,
// D-048; docs/books.md "Bank and payment connections"). A QVariant face over
// books_connect's ConnectionsController.
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "connections.h"
#include "connections_controller.h"

#include <QEventLoop>
#include <QTimer>
#include <QUuid>

#include <memory>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using Connect::ConnectionsController;

namespace {

QString providerName(const QString &provider)
{
    return provider == Provider::Mercury ? QStringLiteral("Mercury") : QStringLiteral("Stripe");
}

bool knownProvider(const QString &provider)
{
    return provider == Provider::Mercury || provider == Provider::Stripe;
}

QVariantMap reportMap(const Connect::SyncReport &r)
{
    return {{QStringLiteral("ok"), r.ok},           {QStringLiteral("imported"), r.imported},
            {QStringLiteral("applied"), r.applied}, {QStringLiteral("matched"), r.matched},
            {QStringLiteral("posted"), r.posted},   {QStringLiteral("errors"), r.errors}};
}

} // namespace

std::optional<QVariantMap>
BooksService::waitFor(const std::function<void(std::function<void(const QVariantMap &)>)> &start, int timeoutMs)
{
    // AGENT-NOTE: connectMercury/connectStripe/disconnect are synchronous
    // for QML (the lead's dialog reads the result of the call), but the
    // provider and the keyring answer asynchronously. A local event loop
    // with user input excluded waits for the answer — painting and the
    // network keep running, the dialog cannot be re-entered — bounded by
    // the HTTP deadline. The callback may outlive this frame (timeout), so
    // it only touches shared state.
    struct State {
        std::optional<QVariantMap> result;
        QEventLoop loop;
    };
    auto state = std::make_shared<State>();
    start([state](const QVariantMap &result) {
        state->result = result;
        state->loop.quit();
    });
    if (!state->result) {
        QTimer::singleShot(timeoutMs, &state->loop, &QEventLoop::quit);
        state->loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return state->result;
}

bool BooksService::syncingConnections() const
{
    return m_connections->syncing();
}

QVariantList BooksService::connections() const
{
    QVariantList out;
    CompanyFile *c = file();
    for (const QString &provider : {Provider::Mercury, Provider::Stripe}) {
        const std::optional<ConnectionRecord> record = c != nullptr ? Connections(*c).get(provider) : std::nullopt;
        QString status = QStringLiteral("disconnected");
        QString summary;
        if (record) {
            if (m_connections->syncing()) {
                status = QStringLiteral("syncing");
            } else if (m_connections->secretState(provider) == ConnectionsController::SecretState::Missing) {
                status = QStringLiteral("needsToken");
            } else if (!record->lastError.isEmpty()) {
                status = QStringLiteral("error");
            } else {
                status = QStringLiteral("connected");
            }
            if (provider == Provider::Mercury) {
                const qsizetype linked = Connect::MercurySettings::fromJson(record->settings).links.size();
                summary = linked == 0 ? tr("Connected — link an account to start syncing")
                                      : tr("%n account(s) linked", nullptr, int(linked));
            } else {
                const Connect::StripeSettings s = Connect::StripeSettings::fromJson(record->settings);
                summary = s.accountName + (s.livemode ? QString() : tr(" (test mode)"));
                if (s.payoutAccountId != 0) {
                    summary += tr(" · payouts to %1").arg(nameOfAccount(s.payoutAccountId));
                }
            }
        }
        out << QVariantMap{
            {QStringLiteral("provider"), provider},
            {QStringLiteral("name"), providerName(provider)},
            {QStringLiteral("status"), status},
            {QStringLiteral("lastSync"),
             record && record->lastSync.isValid() ? record->lastSync.toLocalTime().toString(Qt::ISODate) : QString()},
            {QStringLiteral("lastError"), record ? record->lastError : QString()},
            {QStringLiteral("summary"), summary},
        };
    }
    return out;
}

QVariantMap BooksService::connectMercury(const QString &token)
{
    if (file() == nullptr) {
        return noCompany();
    }
    const MoneyFormat money = moneyFormat();
    const std::optional<QVariantMap> result = waitFor([this, token, money](auto done) {
        m_connections->connectMercury(token, [done, money](const Connect::MercuryConnectOutcome &o) {
            QVariantList accounts;
            for (const Connect::MercuryAccount &a : o.accounts) {
                accounts << QVariantMap{{QStringLiteral("id"), a.id},
                                        {QStringLiteral("name"), a.name},
                                        {QStringLiteral("kind"), a.kind},
                                        {QStringLiteral("lastFour"), a.lastFour},
                                        {QStringLiteral("balance"), a.available},
                                        {QStringLiteral("balanceText"), money.text(a.available)},
                                        {QStringLiteral("currentBalance"), a.current},
                                        {QStringLiteral("currentBalanceText"), money.text(a.current)}};
            }
            done({{QStringLiteral("ok"), o.ok}, {QStringLiteral("error"), o.error},
                  {QStringLiteral("accounts"), accounts}});
        });
    });
    return result.value_or(Format::failure(tr("Mercury did not answer in time. Try again.")));
}

QVariantMap BooksService::linkMercuryAccount(const QString &mercuryAccountId, qint64 booksAccountId)
{
    if (file() == nullptr) {
        return noCompany();
    }
    return Format::result(m_connections->linkMercuryAccount(mercuryAccountId, booksAccountId));
}

QVariantList BooksService::mercuryLinks() const
{
    QVariantList out;
    CompanyFile *c = file();
    const std::optional<ConnectionRecord> record = c != nullptr ? Connections(*c).get(Provider::Mercury) : std::nullopt;
    if (!record) {
        return out;
    }
    const Connect::MercurySettings s = Connect::MercurySettings::fromJson(record->settings);
    for (const Connect::MercuryAccount &a : s.accounts) {
        const qint64 linked = s.links.value(a.id, 0);
        out << QVariantMap{{QStringLiteral("mercuryAccountId"), a.id},
                           {QStringLiteral("name"), a.name},
                           {QStringLiteral("kind"), a.kind},
                           {QStringLiteral("lastFour"), a.lastFour},
                           {QStringLiteral("booksAccountId"), linked},
                           {QStringLiteral("booksAccountName"), nameOfAccount(linked)},
                           {QStringLiteral("linked"), linked != 0}};
    }
    return out;
}

QVariantMap BooksService::connectStripe(const QString &key)
{
    if (file() == nullptr) {
        return noCompany();
    }
    const std::optional<QVariantMap> result = waitFor([this, key](auto done) {
        m_connections->connectStripe(key, [done](const Connect::StripeConnectOutcome &o) {
            done({{QStringLiteral("ok"), o.ok},
                  {QStringLiteral("error"), o.error},
                  {QStringLiteral("accountName"), o.accountName},
                  {QStringLiteral("livemode"), o.livemode}});
        });
    });
    return result.value_or(Format::failure(tr("Stripe did not answer in time. Try again.")));
}

QVariantMap BooksService::stripeSettings() const
{
    CompanyFile *c = file();
    const std::optional<ConnectionRecord> record = c != nullptr ? Connections(*c).get(Provider::Stripe) : std::nullopt;
    const Connect::StripeSettings s =
        Connect::StripeSettings::fromJson(record ? record->settings : QJsonObject());
    return {{QStringLiteral("connected"), record.has_value()},
            {QStringLiteral("clearingAccountId"), s.clearingAccountId},
            {QStringLiteral("clearingAccountName"), nameOfAccount(s.clearingAccountId)},
            {QStringLiteral("feeAccountId"), s.feeAccountId},
            {QStringLiteral("feeAccountName"), nameOfAccount(s.feeAccountId)},
            {QStringLiteral("payoutAccountId"), s.payoutAccountId},
            {QStringLiteral("payoutAccountName"), nameOfAccount(s.payoutAccountId)},
            {QStringLiteral("autoPostFees"), s.autoPostFees},
            {QStringLiteral("autoPostPayouts"), s.autoPostPayouts},
            {QStringLiteral("autoApplyInvoices"), s.autoApplyInvoices},
            {QStringLiteral("accountName"), s.accountName},
            {QStringLiteral("livemode"), s.livemode},
            {QStringLiteral("currency"), s.currency.toUpper()}};
}

QVariantMap BooksService::saveStripeSettings(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<ConnectionRecord> record = Connections(*c).get(Provider::Stripe);
    Connect::StripeSettings s = Connect::StripeSettings::fromJson(record ? record->settings : QJsonObject());
    const auto id = [&map](const char *key, qint64 *field) {
        if (map.contains(QLatin1String(key))) {
            *field = Format::id(map.value(QLatin1String(key)));
        }
    };
    const auto flag = [&map](const char *key, bool *field) {
        if (map.contains(QLatin1String(key))) {
            *field = map.value(QLatin1String(key)).toBool();
        }
    };
    id("clearingAccountId", &s.clearingAccountId);
    id("feeAccountId", &s.feeAccountId);
    id("payoutAccountId", &s.payoutAccountId);
    flag("autoPostFees", &s.autoPostFees);
    flag("autoPostPayouts", &s.autoPostPayouts);
    flag("autoApplyInvoices", &s.autoApplyInvoices);
    return Format::result(m_connections->saveStripeSettings(s));
}

QString BooksService::syncConnections()
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QPointer<BooksService> self(this);
    m_connections->sync([self, requestId](const Connect::SyncReport &report) {
        // Queued: the caller has the request id before the answer arrives,
        // also when the sync fails fast.
        QTimer::singleShot(0, self, [self, requestId, report] {
            if (!self) {
                return;
            }
            emit self->connectionSyncFinished(requestId, reportMap(report));
            emit self->connectionsChanged();
            if (report.ok) {
                emit self->notify(tr("Bank sync"),
                                  tr("%n new transaction(s); %1 invoice payment(s) applied, %2 matched.", nullptr,
                                     report.imported)
                                      .arg(report.applied)
                                      .arg(report.matched),
                                  report.errors.isEmpty() ? QStringLiteral("success") : QStringLiteral("warning"));
            } else {
                emit self->notify(tr("Bank sync"), report.errors.join(QLatin1Char('\n')), QStringLiteral("danger"));
            }
        });
    });
    emit connectionsChanged();
    return requestId;
}

QVariantMap BooksService::disconnect(const QString &provider)
{
    if (file() == nullptr) {
        return noCompany();
    }
    if (!knownProvider(provider)) {
        return Format::failure(tr("Unknown connection \"%1\".").arg(provider));
    }
    const std::optional<QVariantMap> result = waitFor([this, provider](auto done) {
        m_connections->disconnect(provider, [done](bool ok, const QString &error) {
            done({{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}});
        });
    });
    return result.value_or(Format::failure(tr("The keyring did not answer in time.")));
}

QString BooksService::createPaymentLink(qint64 invoiceId)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QPointer<BooksService> self(this);
    m_connections->createPaymentLink(invoiceId, [self, requestId](const Connect::PaymentLinkOutcome &o) {
        QTimer::singleShot(0, self, [self, requestId, o] {
            if (!self) {
                return;
            }
            emit self->paymentLinkFinished(requestId, {{QStringLiteral("ok"), o.ok},
                                                       {QStringLiteral("url"), o.url},
                                                       {QStringLiteral("error"), o.error},
                                                       {QStringLiteral("reused"), o.reused}});
            if (o.ok) {
                emit self->changed(); // document(id).paymentLinkUrl
            }
        });
    });
    return requestId;
}

QString BooksService::payOnlineLine(qint64 documentId) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return QString();
    }
    const std::optional<PaymentLinkRecord> link = Connections(*c).currentPaymentLink(documentId);
    return link ? tr("Pay online: %1").arg(link->url) : QString();
}

} // namespace QindaOffice::Apps::Books
