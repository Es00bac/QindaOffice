// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: Clean Up Conversation and Ignore Conversation
// (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "mail_service.h"
#include "provider_calls.h"

#include <qindatk/local_productivity_store.h>

#include <QRegularExpression>
#include <algorithm>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;

namespace {

constexpr int kMaxIgnored = 1000;

// A body as comparable text: quote markers, line breaks and spacing gone,
// lower case — a reply that quotes a message contains its text this way.
QString comparable(const QString &body)
{
    QString text = body;
    text.remove(QRegularExpression(u"^[ \\t]*(>[ \\t]?)+"_s, QRegularExpression::MultilineOption));
    return text.simplified().toLower();
}

// The text a message adds: its body without the part it quotes (the lines
// from "On … wrote:" / a quoted block / "-----Original Message-----" on).
QString ownText(const QString &body)
{
    static const QRegularExpression cut(
        u"(^-{2,}\\s*Original Message\\s*-{2,}|^On .+wrote:\\s*$|^>)"_s,
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = cut.match(body);
    return comparable(match.hasMatch() ? body.left(match.capturedStart()) : body);
}

} // namespace

QString MailOrganizer::cleanUpConversation(const QString &messageId)
{
    const QVariantMap selected = m_service->messageById(messageId);
    const QString thread = selected.value(u"threadId"_s).toString();
    QList<QVariantMap> messages;
    for (const QVariant &value : m_service->conversation(thread.isEmpty() ? messageId : thread)) {
        const QVariantMap message = value.toMap();
        const AccountRuntime *rt = m_service->runtime(message.value(u"accountId"_s).toString());
        const QString trash = rt != nullptr ? rt->specialMailbox(u"trash"_s) : u"Trash"_s;
        if (m_service->folderKeyOf(message).endsWith(u':' + trash)) continue;
        messages.append(message);
    }
    std::sort(messages.begin(), messages.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(u"date"_s).toString() < b.value(u"date"_s).toString();
    });
    QStringList redundant;
    for (qsizetype i = 0; i + 1 < messages.size(); ++i) {
        const QVariantMap &message = messages.at(i);
        // Outlook's defaults: unread, flagged and categorised mail stays.
        if (message.value(u"unread"_s).toBool() || message.value(u"flagged"_s).toBool()
            || !message.value(u"categories"_s).toStringList().isEmpty())
            continue;
        const QString own = ownText(message.value(u"bodyPlain"_s).toString());
        if (own.size() < 2) continue;
        for (qsizetype j = i + 1; j < messages.size(); ++j) {
            if (comparable(messages.at(j).value(u"bodyPlain"_s).toString()).contains(own)) {
                redundant.append(message.value(u"id"_s).toString());
                break;
            }
        }
    }
    emit m_service->notify(redundant.isEmpty() ? tr("Nothing to clean up") : tr("Conversation cleaned up"),
                           redundant.isEmpty() ? tr("Every message of this conversation says something the others do not.")
                                               : tr("%n redundant message(s) moved to Trash.", nullptr, int(redundant.size())),
                           u"info"_s);
    return m_service->deleteMessages(redundant);
}

bool MailOrganizer::isIgnored(const QString &messageId) const
{
    const QString thread = m_service->messageById(messageId).value(u"threadId"_s).toString();
    return !thread.isEmpty() && m_ignored.contains(thread);
}

QString MailOrganizer::ignoreConversation(const QString &messageId)
{
    const QString thread = m_service->messageById(messageId).value(u"threadId"_s).toString();
    if (thread.isEmpty()) return m_service->beginBatch(u"ignore"_s, 0);
    if (m_ignored.contains(thread)) {
        // Asked again: stop ignoring (the messages stay where they are now).
        m_ignored.removeAll(thread);
        save();
        bump();
        emit m_service->notify(tr("No longer ignored"), tr("New messages of this conversation arrive in the Inbox again."), u"info"_s);
        return m_service->beginBatch(u"ignore"_s, 0);
    }
    m_ignored.append(thread);
    while (m_ignored.size() > kMaxIgnored) m_ignored.removeFirst();
    save();
    bump();
    QStringList ids;
    for (const QVariant &value : m_service->conversation(thread)) {
        const QVariantMap message = value.toMap();
        const AccountRuntime *rt = m_service->runtime(message.value(u"accountId"_s).toString());
        const QString trash = rt != nullptr ? rt->specialMailbox(u"trash"_s) : u"Trash"_s;
        if (!m_service->folderKeyOf(message).endsWith(u':' + trash)) ids.append(message.value(u"id"_s).toString());
    }
    emit m_service->notify(tr("Conversation ignored"),
                           tr("Its messages, and new ones in it, go to Trash. Ignore it again to stop."), u"info"_s);
    return m_service->deleteMessages(ids);
}

} // namespace QindaOffice::Apps::Mail
