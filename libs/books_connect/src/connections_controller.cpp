// SPDX-License-Identifier: GPL-3.0-or-later
// ConnectionsController: connect, link, settings, disconnect and the
// keyring state (connections_controller.h has the contract).
#include "connections_controller.h"

#include "accounts.h"
#include "company_file.h"
#include "connections.h"
#include "mercury_client.h"
#include "stripe_client.h"

#include <QNetworkAccessManager>

namespace QindaOffice::Books::Connect {

namespace {

// An account of `type` named `name`, created when the chart has none.
qint64 ensureAccount(CompanyFile &file, const QString &name, AccountType type, const QString &description,
                     QString *error)
{
    ChartOfAccounts chart(file);
    for (const Account &a : chart.accountsOfType(type, true)) {
        if (a.name.compare(name, Qt::CaseInsensitive) == 0) {
            return a.id;
        }
    }
    Account account;
    account.name = name;
    account.type = type;
    account.description = description;
    const Result created = chart.create(account);
    if (!created.ok && error != nullptr) {
        *error = created.error;
    }
    return created.ok ? created.id : 0;
}

QString capitalized(const QString &word)
{
    return word.isEmpty() ? word : word.left(1).toUpper() + word.mid(1);
}

} // namespace

qint64 ensureStripeClearingAccount(CompanyFile &file, QString *error)
{
    return ensureAccount(file, QStringLiteral("Stripe (%1)").arg(file.currency()), AccountType::Bank,
                         QStringLiteral("Clearing account for the Stripe balance (QindaBooks connection)"), error);
}

qint64 ensureMerchantFeesAccount(CompanyFile &file, QString *error)
{
    return ensureAccount(file, QStringLiteral("Merchant Fees"), AccountType::Expense,
                         QStringLiteral("Card processing fees (Stripe)"), error);
}

ConnectionsController::ConnectionsController(FileProvider file, std::shared_ptr<SecretBackend> secrets,
                                             QObject *parent)
    : QObject(parent)
    , m_file(std::move(file))
    , m_secrets(std::move(secrets))
    , m_network(new QNetworkAccessManager(this))
    , m_mercuryBase(MercuryClient::defaultBaseUrl())
    , m_stripeBase(StripeClient::defaultBaseUrl())
{
}

ConnectionsController::~ConnectionsController() = default;

void ConnectionsController::setEndpoints(const QUrl &mercuryBase, const QUrl &stripeBase)
{
    m_mercuryBase = mercuryBase;
    m_stripeBase = stripeBase;
}

ConnectionsController::SecretState ConnectionsController::secretState(const QString &provider) const
{
    return m_secretStates.value(provider, SecretState::Unknown);
}

void ConnectionsController::setSecretState(const QString &provider, SecretState state)
{
    if (m_secretStates.value(provider, SecretState::Unknown) != state) {
        m_secretStates.insert(provider, state);
        emit stateChanged();
    }
}

void ConnectionsController::readSecret(const QString &provider, std::function<void(const SecretOutcome &)> done)
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return done(SecretOutcome{SecretOutcome::Status::Failed, {}, QStringLiteral("No company is open.")});
    }
    const QPointer<ConnectionsController> self(this);
    m_secrets->read(secretKey(Connections(*file).companyUuid(), provider),
                    [self, provider, done](const SecretOutcome &outcome) {
                        if (!self) {
                            return;
                        }
                        if (outcome.ok()) {
                            self->setSecretState(provider, SecretState::Present);
                        } else if (outcome.status == SecretOutcome::Status::NotFound) {
                            self->setSecretState(provider, SecretState::Missing);
                        }
                        done(outcome);
                    });
}

void ConnectionsController::refreshSecretStates()
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return;
    }
    // AGENT-NOTE: only providers that have a row are checked, so a company
    // without connections never touches the keyring (tests, first run).
    for (const ConnectionRecord &r : Connections(*file).all()) {
        readSecret(r.provider, [](const SecretOutcome &) {});
    }
}

void ConnectionsController::connectMercury(const QString &token, std::function<void(const MercuryConnectOutcome &)> done)
{
    const QString secret = token.trimmed();
    if (secret.isEmpty() || m_file() == nullptr) {
        return done(MercuryConnectOutcome{false, secret.isEmpty() ? QStringLiteral("Paste a Mercury API token.")
                                                                  : QStringLiteral("No company is open."), {}});
    }
    // AGENT-NOTE: Mercury accounts are US-dollar accounts; their amounts
    // are only meaningful in a USD company (one company, one currency).
    if (m_file()->currency() != QLatin1String("USD")) {
        return done(MercuryConnectOutcome{
            false, QStringLiteral("Mercury accounts are in US dollars; these books are kept in %1.").arg(m_file()->currency()),
            {}});
    }
    auto *client = new MercuryClient(m_network, m_mercuryBase, secret, this);
    const QPointer<ConnectionsController> self(this);
    client->accounts([self, client, secret, done](const ApiResponse &r, const QList<MercuryAccount> &accounts) {
        client->deleteLater();
        if (!self) {
            return;
        }
        if (!r.ok()) {
            return done(MercuryConnectOutcome{false, r.error, {}});
        }
        CompanyFile *file = self->m_file();
        if (file == nullptr) {
            return done(MercuryConnectOutcome{false, QStringLiteral("The company was closed."), {}});
        }
        const QString key = secretKey(Connections(*file).companyUuid(), Provider::Mercury);
        self->m_secrets->write(key, secret, [self, accounts, done](const SecretOutcome &written) {
            if (!self) {
                return;
            }
            CompanyFile *file = self->m_file();
            if (!written.ok() || file == nullptr) {
                return done(MercuryConnectOutcome{false, written.ok() ? QStringLiteral("The company was closed.")
                                                                      : written.error, {}});
            }
            Connections connections(*file);
            ConnectionRecord record = connections.get(Provider::Mercury).value_or(ConnectionRecord{});
            record.provider = Provider::Mercury;
            MercurySettings settings = MercurySettings::fromJson(record.settings);
            settings.accounts = accounts;
            if (!settings.startDate.isValid()) {
                settings.startDate = self->today().addDays(-kDefaultHistoryDays);
            }
            record.settings = settings.toJson();
            record.status = QStringLiteral("connected");
            record.lastError.clear();
            const Result saved = connections.save(record);
            self->setSecretState(Provider::Mercury, SecretState::Present);
            emit self->stateChanged();
            done(MercuryConnectOutcome{saved.ok, saved.error, accounts});
        });
    });
}

void ConnectionsController::connectStripe(const QString &key, std::function<void(const StripeConnectOutcome &)> done)
{
    const QString secret = key.trimmed();
    if (secret.isEmpty() || m_file() == nullptr) {
        return done(StripeConnectOutcome{false, secret.isEmpty() ? QStringLiteral("Paste a Stripe API key.")
                                                                 : QStringLiteral("No company is open."), {}, false});
    }
    auto *client = new StripeClient(m_network, m_stripeBase, secret, this);
    const QPointer<ConnectionsController> self(this);
    client->balance([self, client, secret, done](const ApiResponse &r, const StripeBalance &balance) {
        if (!self || !r.ok()) {
            client->deleteLater();
            if (self) {
                done(StripeConnectOutcome{false, r.error, {}, false});
            }
            return;
        }
        client->accountName([self, client, secret, balance, done](const ApiResponse &, const QString &name) {
            client->deleteLater();
            if (!self) {
                return;
            }
            CompanyFile *file = self->m_file();
            if (file == nullptr) {
                return done(StripeConnectOutcome{false, QStringLiteral("The company was closed."), {}, false});
            }
            const QString accountName = name.isEmpty() ? QStringLiteral("Stripe") : name;
            const QString secretName = secretKey(Connections(*file).companyUuid(), Provider::Stripe);
            self->m_secrets->write(secretName, secret, [self, balance, accountName, done](const SecretOutcome &w) {
                if (!self) {
                    return;
                }
                CompanyFile *file = self->m_file();
                if (!w.ok() || file == nullptr) {
                    return done(StripeConnectOutcome{false, w.ok() ? QStringLiteral("The company was closed.") : w.error,
                                                     {}, false});
                }
                Connections connections(*file);
                ConnectionRecord record = connections.get(Provider::Stripe).value_or(ConnectionRecord{});
                record.provider = Provider::Stripe;
                StripeSettings s = StripeSettings::fromJson(record.settings);
                QString error;
                if (s.clearingAccountId == 0) {
                    s.clearingAccountId = ensureStripeClearingAccount(*file, &error);
                }
                if (s.feeAccountId == 0) {
                    s.feeAccountId = ensureMerchantFeesAccount(*file, &error);
                }
                if (s.payoutAccountId == 0) {
                    // The Mercury-linked checking account when there is one.
                    const MercurySettings mercury = MercurySettings::fromJson(
                        connections.get(Provider::Mercury).value_or(ConnectionRecord{}).settings);
                    s.payoutAccountId = mercury.links.isEmpty() ? 0 : *std::min_element(mercury.links.cbegin(),
                                                                                         mercury.links.cend());
                }
                s.currency = file->currency().toLower();
                s.accountName = accountName;
                s.livemode = balance.livemode;
                if (!s.startDate.isValid()) {
                    s.startDate = self->today().addDays(-kDefaultHistoryDays);
                }
                record.settings = s.toJson();
                record.status = QStringLiteral("connected");
                record.lastError.clear();
                const Result saved = connections.save(record);
                self->setSecretState(Provider::Stripe, SecretState::Present);
                emit self->stateChanged();
                done(StripeConnectOutcome{saved.ok && error.isEmpty(), saved.ok ? error : saved.error, accountName,
                                          balance.livemode});
            });
        });
    });
}

Result ConnectionsController::linkMercuryAccount(const QString &mercuryAccountId, qint64 booksAccountId)
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("No company is open."));
    }
    Connections connections(*file);
    std::optional<ConnectionRecord> record = connections.get(Provider::Mercury);
    if (!record) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("Connect Mercury first."));
    }
    MercurySettings settings = MercurySettings::fromJson(record->settings);
    const auto known = std::find_if(settings.accounts.cbegin(), settings.accounts.cend(),
                                    [&](const MercuryAccount &a) { return a.id == mercuryAccountId; });
    if (known == settings.accounts.cend()) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("That Mercury account is not in the list."));
    }
    ChartOfAccounts chart(*file);
    qint64 target = booksAccountId;
    if (booksAccountId < 0) {
        settings.links.remove(mercuryAccountId);
        target = 0;
    } else if (booksAccountId == 0) {
        QString name = QStringLiteral("Mercury %1 ••%2")
                           .arg(capitalized(known->kind.isEmpty() ? QStringLiteral("account") : known->kind),
                                known->lastFour);
        for (int n = 2; chart.find(name) != 0; ++n) {
            name = QStringLiteral("Mercury %1 ••%2 (%3)").arg(capitalized(known->kind), known->lastFour).arg(n);
        }
        Account account;
        account.name = name;
        account.type = AccountType::Bank;
        account.description = QStringLiteral("Mercury %1 (linked for bank sync)").arg(known->name);
        const Result created = chart.create(account);
        if (!created.ok) {
            return created;
        }
        target = created.id;
    } else {
        const std::optional<Account> account = chart.account(booksAccountId);
        if (!account || (account->type != AccountType::Bank && account->type != AccountType::CreditCard)) {
            return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("Link a bank or credit card account."));
        }
    }
    if (target != 0) {
        for (auto it = settings.links.begin(); it != settings.links.end();) {
            it = (it.value() == target && it.key() != mercuryAccountId) ? settings.links.erase(it) : std::next(it);
        }
        settings.links.insert(mercuryAccountId, target);
    }
    record->settings = settings.toJson();
    const Result saved = connections.save(*record);
    if (!saved.ok) {
        return saved;
    }
    // Stripe connected before any Mercury link: payouts now have a home.
    if (std::optional<ConnectionRecord> stripe = connections.get(Provider::Stripe); stripe && target != 0) {
        StripeSettings s = StripeSettings::fromJson(stripe->settings);
        if (s.payoutAccountId == 0) {
            s.payoutAccountId = target;
            stripe->settings = s.toJson();
            connections.save(*stripe);
        }
    }
    emit stateChanged();
    return Result::success(target);
}

Result ConnectionsController::saveStripeSettings(const StripeSettings &settings)
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("No company is open."));
    }
    Connections connections(*file);
    std::optional<ConnectionRecord> record = connections.get(Provider::Stripe);
    if (!record) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("Connect Stripe first."));
    }
    ChartOfAccounts chart(*file);
    const auto isType = [&chart](qint64 id, std::initializer_list<AccountType> types) {
        const std::optional<Account> a = chart.account(id);
        return a && std::find(types.begin(), types.end(), a->type) != types.end();
    };
    if (!isType(settings.clearingAccountId, {AccountType::Bank})) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("The Stripe clearing account must be a bank account."));
    }
    if (settings.feeAccountId != 0
        && !isType(settings.feeAccountId, {AccountType::Expense, AccountType::CostOfGoodsSold, AccountType::OtherExpense})) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("Merchant fees go to an expense account."));
    }
    if (settings.payoutAccountId != 0
        && (settings.payoutAccountId == settings.clearingAccountId || !isType(settings.payoutAccountId, {AccountType::Bank}))) {
        return Result::failure(ErrorCode::UnknownAccount,
                               QStringLiteral("Payouts go to a bank account other than the Stripe account."));
    }
    StripeSettings merged = StripeSettings::fromJson(record->settings);
    merged.clearingAccountId = settings.clearingAccountId;
    merged.feeAccountId = settings.feeAccountId;
    merged.payoutAccountId = settings.payoutAccountId;
    merged.autoPostFees = settings.autoPostFees;
    merged.autoPostPayouts = settings.autoPostPayouts;
    merged.autoApplyInvoices = settings.autoApplyInvoices;
    record->settings = merged.toJson();
    const Result saved = connections.save(*record);
    if (saved.ok) {
        emit stateChanged();
    }
    return saved;
}

void ConnectionsController::disconnect(const QString &provider, std::function<void(bool, const QString &)> done)
{
    CompanyFile *file = m_file();
    if (file == nullptr) {
        return done(false, QStringLiteral("No company is open."));
    }
    const QString key = secretKey(Connections(*file).companyUuid(), provider);
    const Result removed = Connections(*file).remove(provider);
    const QPointer<ConnectionsController> self(this);
    m_secrets->remove(key, [self, provider, removed, done](const SecretOutcome &outcome) {
        if (!self) {
            return;
        }
        self->m_secretStates.remove(provider);
        emit self->stateChanged();
        const bool ok = removed.ok && outcome.ok();
        done(ok, !removed.ok ? removed.error : outcome.error);
    });
}

} // namespace QindaOffice::Books::Connect
