// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: automatic replies (Microsoft Graph, Exchange Web Services
// or ManageSieve, else plainly not available) and the server's own inbox
// rules, shown read-only (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "mail_service.h"
#include "provider_calls.h"

#include "account_store.h"
#include "auto_reply.h"
#include "exchange_mailbox.h"
#include "json_settings.h"
#include "managesieve_provider.h"

#include <memory>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

struct Way {
    QString method;     // graph | ews | sieve | none
    QString service;    // AccountRuntime::run service
    QString reason;     // why there is none
};

} // namespace

static Way wayFor(AccountRuntime *rt)
{
    if (rt == nullptr) return {u"none"_s, {}, MailOrganizer::tr("Mail on this computer has no server to answer for you.")};
    const QString mailbox = rt->mailboxService();
    if (mailbox == "graph"_L1) return {u"graph"_s, u"graphmail"_s, {}};
    if (mailbox == "ews"_L1) return {u"ews"_s, u"ews"_s, {}};
    if (rt->isImap()) return {u"sieve"_s, u"sieve"_s, {}};
    if (rt->isPop3())
        return {u"none"_s, {}, MailOrganizer::tr("A POP3 account has no server-side filters, so its server cannot send "
                                                 "automatic replies. Set them up in your provider's webmail.")};
    return {u"none"_s, {}, MailOrganizer::tr("This account has no mail server.")};
}

QVariantList MailOrganizer::autoReplyAccounts() const
{
    QVariantList out;
    for (const QindaOffice::Mail::MailAccount &account : m_service->accountStore()->list()) {
        if (!account.hasMail() && account.kind != QLatin1String(QindaOffice::Mail::kKindExchange)) continue;
        const Way way = wayFor(m_service->runtime(account.id));
        out.append(QVariantMap{{u"id"_s, account.id}, {u"name"_s, account.displayName}, {u"address"_s, account.emailAddress},
                               {u"method"_s, way.method}, {u"reason"_s, way.reason}});
    }
    return out;
}

QString MailOrganizer::readAutoReplies(const QString &accountId)
{
    const QString requestId = ProviderCalls::newRequestId(u"replies"_s);
    AccountRuntime *rt = m_service->runtime(accountId);
    const Way way = wayFor(rt);
    // The text last set here (a Sieve reply that is off keeps none on the server).
    const QVariantMap remembered = m_file->value(u"autoReplies"_s).toMap().value(accountId).toMap();
    if (way.method == "none"_L1) {
        answerLater("replies", requestId, {{u"ok"_s, false}, {u"state"_s, u"rejected"_s}, {u"available"_s, false},
                                           {u"method"_s, way.method}, {u"error"_s, way.reason}, {u"settings"_s, remembered}});
        return requestId;
    }
    QPointer<MailOrganizer> self(this);
    const auto finish = [self, requestId, way, remembered](const QVariantMap &result) {
        if (self == nullptr) return;
        QVariantMap out = result;
        out.insert(u"method"_s, way.method);
        if (!out.contains(u"available"_s)) out.insert(u"available"_s, true);
        QVariantMap settings = out.value(u"settings"_s).toMap();
        if (!settings.value(u"enabled"_s).toBool() && settings.value(u"internalMessage"_s).toString().isEmpty()) {
            for (auto it = remembered.cbegin(); it != remembered.cend(); ++it)
                if (it.key() != "enabled"_L1) settings.insert(it.key(), it.value());
        }
        out.insert(u"settings"_s, settings);
        if (out.value(u"consentRequired"_s).toBool())
            out.insert(u"error"_s, tr("Sign in again (Accounts ▸ Account Settings) to let QindaMail manage this mailbox's "
                                      "automatic replies."));
        if (way.method == "sieve"_L1 && out.value(u"state"_s).toString() == "offline"_L1 && self->m_service->online()) {
            // Online, yet nothing answers on ManageSieve's port: say so plainly.
            out.insert(u"available"_s, false);
            out.insert(u"error"_s, tr("The mail server does not offer ManageSieve (RFC 5804), so QindaMail cannot set "
                                      "automatic replies there: %1").arg(out.value(u"error"_s).toString()));
        }
        self->answerLater("replies", requestId, out);
    };
    if (way.method == "sieve"_L1) {
        M::ManageSieveProvider *sieve = rt->sieve();
        rt->run(way.service, sieve, [sieve](const QString &rid) { return sieve->readVacation(rid); }, finish, false);
    } else {
        M::ExchangeMailbox *box = rt->mailbox();
        rt->run(way.service, box, [box](const QString &rid) { return box->readAutoReplies(rid); }, finish, false);
    }
    return requestId;
}

QString MailOrganizer::writeAutoReplies(const QString &accountId, const QVariantMap &map)
{
    const QString requestId = ProviderCalls::newRequestId(u"replies"_s);
    AccountRuntime *rt = m_service->runtime(accountId);
    const Way way = wayFor(rt);
    const M::AutoReply reply = M::AutoReplies::fromVariantMap(map);
    const QStringList problems = M::AutoReplies::validate(reply);
    if (way.method == "none"_L1 || !problems.isEmpty()) {
        answerLater("replies", requestId, {{u"ok"_s, false}, {u"state"_s, u"rejected"_s}, {u"method"_s, way.method},
                                           {u"available"_s, way.method != "none"_L1},
                                           {u"error"_s, problems.isEmpty() ? way.reason : problems.first().section(u": "_s, 1)}});
        return requestId;
    }
    QVariantMap remembered = m_file->value(u"autoReplies"_s).toMap();
    remembered.insert(accountId, M::AutoReplies::toVariantMap(reply));
    m_file->setValue(u"autoReplies"_s, remembered);
    save();
    QPointer<MailOrganizer> self(this);
    const auto finish = [self, requestId, way](const QVariantMap &result) {
        if (self == nullptr) return;
        QVariantMap out = result;
        out.insert(u"method"_s, way.method);
        if (out.value(u"consentRequired"_s).toBool())
            out.insert(u"error"_s, tr("Sign in again (Accounts ▸ Account Settings) to let QindaMail manage this mailbox's "
                                      "automatic replies."));
        self->answerLater("replies", requestId, out);
    };
    const QVariantMap settings = M::AutoReplies::toVariantMap(reply);
    if (way.method == "sieve"_L1) {
        M::ManageSieveProvider *sieve = rt->sieve();
        const QStringList addresses{rt->account().emailAddress};
        rt->run(way.service, sieve, [sieve, settings, addresses](const QString &rid) {
            return sieve->writeVacation(rid, settings, addresses);
        }, finish, false);
    } else {
        M::ExchangeMailbox *box = rt->mailbox();
        rt->run(way.service, box, [box, settings](const QString &rid) { return box->writeAutoReplies(rid, settings); }, finish, false);
    }
    return requestId;
}

void MailOrganizer::refreshServerRules()
{
    auto collected = std::make_shared<QVariantList>();
    auto pending = std::make_shared<int>(1);
    QPointer<MailOrganizer> self(this);
    const auto release = [self, collected, pending] {
        if (--*pending > 0 || self == nullptr) return;
        if (*collected == self->m_serverRules) return;
        self->m_serverRules = *collected;
        emit self->serverRulesChanged();
    };
    for (const QindaOffice::Mail::MailAccount &account : m_service->accountStore()->list()) {
        AccountRuntime *rt = m_service->runtime(account.id);
        M::ExchangeMailbox *box = rt != nullptr ? rt->mailbox() : nullptr;
        if (box == nullptr || m_mailboxBlocked.contains(account.id)) continue;
        ++*pending;
        const QString name = account.displayName, id = account.id;
        rt->run(rt->mailboxService() == "graph"_L1 ? u"graphmail"_s : u"ews"_s, box,
                [box](const QString &rid) { return box->listServerRules(rid); },
                [collected, release, name, id](const QVariantMap &result) {
                    for (const QVariant &value : result.value(u"rules"_s).toList()) {
                        QVariantMap rule = value.toMap();
                        rule.insert(u"accountId"_s, id);
                        rule.insert(u"accountName"_s, name);
                        collected->append(rule);
                    }
                    release();
                },
                false);
    }
    release();
}

} // namespace QindaOffice::Apps::Mail
