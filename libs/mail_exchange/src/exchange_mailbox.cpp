// SPDX-License-Identifier: GPL-3.0-or-later
// ExchangeMailbox: the QObject surface over MailboxJobs (exchange_mailbox.h).
#include "exchange_mailbox.h"

#include "exchange_common.h"
#include "mailbox_jobs.h"

#include <qindatk/protocol_client.h>

#include <QMetaObject>
#include <QUrl>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;
using MailboxJobs::Op;

ExchangeMailbox::ExchangeMailbox(QObject *parent) : QObject(parent) {}

ExchangeMailbox::~ExchangeMailbox()
{
    m_secret.fill(u'\0');
}

void ExchangeMailbox::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret.fill(u'\0');
    m_secret = secret;
    m_mechanism = mechanism.trimmed().toLower();
    emit secretChanged();
}

void ExchangeMailbox::clearSecret()
{
    m_secret.fill(u'\0');
    m_secret.clear();
    emit secretChanged();
}

bool ExchangeMailbox::readAutoReplies(const QString &requestId)
{
    return start(int(Op::ReadAutoReplies), Reply::AutoReplies, requestId, {});
}

bool ExchangeMailbox::writeAutoReplies(const QString &requestId, const QVariantMap &settings)
{
    return start(int(Op::WriteAutoReplies), Reply::AutoReplies, requestId, settings);
}

bool ExchangeMailbox::listCategories(const QString &requestId)
{
    return start(int(Op::ListCategories), Reply::Categories, requestId, {});
}

bool ExchangeMailbox::addCategory(const QString &requestId, const QString &name, int preset)
{
    if (name.trimmed().isEmpty() || name.size() > 255) {
        deliver(Reply::Categories, requestId, rejection(u"Give the category a name (at most 255 characters)"_s));
        return false;
    }
    return start(int(Op::AddCategory), Reply::Categories, requestId,
                 {{u"name"_s, name.trimmed()}, {u"preset"_s, preset}});
}

bool ExchangeMailbox::listServerRules(const QString &requestId)
{
    return start(int(Op::ListRules), Reply::Rules, requestId, {});
}

bool ExchangeMailbox::setItemState(const QString &requestId, const QVariantMap &item)
{
    const QString kind = item.value(u"kind"_s).toString();
    const bool message = kind == "message"_L1 && !item.value(u"internetMessageId"_s).toString().trimmed().isEmpty();
    const bool event = kind == "event"_L1 && !item.value(u"remoteId"_s).toString().trimmed().isEmpty();
    if (!message && !event) {
        deliver(Reply::ItemState, requestId, rejection(u"Name a message (Internet message id) or a synced event"_s));
        return false;
    }
    if (!item.contains(u"categories"_s) && !item.contains(u"flag"_s)) {
        deliver(Reply::ItemState, requestId, rejection(u"Nothing to change"_s));
        return false;
    }
    return start(int(Op::SetItemState), Reply::ItemState, requestId, item);
}

bool ExchangeMailbox::pullItemStates(const QString &requestId, int days)
{
    return start(int(Op::PullItemStates), Reply::ItemState, requestId, {{u"days"_s, qBound(1, days, 365)}});
}

bool ExchangeMailbox::start(int op, Reply reply, const QString &requestId, const QVariantMap &input)
{
    MailboxJobs::Config config;
    config.service = m_service.trimmed().toLower();
    config.baseUrl = QUrl(m_baseUrl.trimmed());
    config.url = QUrl(m_url.trimmed());
    config.userName = m_userName.trimmed();
    config.mailbox = m_mailbox.trimmed();
    config.serverVersion = m_serverVersion.isEmpty() ? u"Exchange2013_SP1"_s : m_serverVersion;
    config.secret = m_secret;
    config.mechanism = m_mechanism;
    config.timeoutMs = m_timeoutMs;
    config.allowInsecureAuth = m_allowInsecureAuth;
    QString invalid;
    if (config.service != "graph"_L1 && config.service != "ews"_L1) invalid = u"service must be graph or ews"_s;
    else if (config.service == "graph"_L1 && !config.baseUrl.isValid()) invalid = u"No Microsoft Graph address is set"_s;
    else if (config.service == "ews"_L1 && (!config.url.isValid() || config.url.isEmpty()))
        invalid = u"No Exchange Web Services address is set"_s;
    else if (config.secret.isEmpty()) invalid = u"The account has no credentials"_s;
    else if (config.service == "graph"_L1 && config.mechanism != "bearer"_L1)
        invalid = u"Microsoft Graph needs a bearer token"_s;
    else if (config.service == "ews"_L1 && config.mechanism == "basic"_L1 && config.userName.isEmpty())
        invalid = u"Exchange basic sign-in needs a user name"_s;
    if (!invalid.isEmpty()) {
        deliver(reply, requestId, rejection(invalid));
        return false;
    }
    ++m_pending;
    emit stateChanged();
    const Op operation = Op(op);
    QindaTK::Protocol::startWorker(
        this, [operation, config, input] { return MailboxJobs::run(operation, config, input); },
        [this, reply, requestId](const QVariantMap &result) {
            m_pending = qMax(0, m_pending - 1);
            emit stateChanged();
            QVariantMap out = result;
            out.insert(u"requestId"_s, requestId);
            switch (reply) {
            case Reply::AutoReplies: emit autoRepliesFinished(requestId, out); break;
            case Reply::Categories: emit categoriesFinished(requestId, out); break;
            case Reply::Rules: emit serverRulesFinished(requestId, out); break;
            case Reply::ItemState: emit itemStateFinished(requestId, out); break;
            }
        });
    return true;
}

void ExchangeMailbox::deliver(Reply reply, const QString &requestId, const QVariantMap &result)
{
    QVariantMap out = result;
    out.insert(u"requestId"_s, requestId);
    // Always on a later turn: the caller holds its request id first.
    QMetaObject::invokeMethod(this, [this, reply, requestId, out] {
        switch (reply) {
        case Reply::AutoReplies: emit autoRepliesFinished(requestId, out); break;
        case Reply::Categories: emit categoriesFinished(requestId, out); break;
        case Reply::Rules: emit serverRulesFinished(requestId, out); break;
        case Reply::ItemState: emit itemStateFinished(requestId, out); break;
        }
    }, Qt::QueuedConnection);
}

} // namespace QindaOffice::Mail
