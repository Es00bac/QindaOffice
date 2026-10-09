// SPDX-License-Identifier: GPL-3.0-or-later
// MailboxJobs over Exchange Web Services (mailbox_jobs.h): Get/Set
// UserOofSettings, the CategoryList user configuration (read-only),
// GetInboxRules, and a message's (FindItem by message:InternetMessageId)
// or a calendar item's categories and flag through UpdateItem.
// References: the EWS operation pages on Microsoft Learn.
#include "mailbox_jobs.h"

#include "ews_soap.h"
#include "xml_tree.h"

#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>
#include <utility>

namespace QindaOffice::Mail::MailboxJobs {

using namespace Qt::StringLiterals;
using Failure = QindaTK::Protocol::Failure;

namespace {

QString utc(const QDateTime &time) { return time.toUTC().toString(u"yyyy-MM-dd'T'HH:mm:ss'Z'"_s); }

struct EwsCall {
    explicit EwsCall(const Config &c) : config(c), transport(c.timeoutMs, c.allowInsecureAuth)
    {
        header.serverVersion = c.serverVersion;
        if (c.mechanism == "basic"_L1)
            transport.setAuthorization("Basic " + (c.userName + u':' + c.secret).toUtf8().toBase64(), {c.secret});
        else
            transport.setAuthorization("Bearer " + c.secret.toUtf8(), {c.secret});
    }

    // One SOAP request; false for a failed dialogue, a fault or throttling.
    bool call(const QString &operation, const Ews::BodyWriter &body, Ews::Response *response)
    {
        HttpTransport::Headers headers{{"Content-Type", "text/xml; charset=utf-8"}, {"Accept", "text/xml"},
                                       {"SOAPAction", Ews::soapAction(operation)}};
        const QString anchor = config.mailbox.isEmpty() ? config.userName : config.mailbox;
        if (!anchor.isEmpty()) headers.append(qMakePair(QByteArray("X-AnchorMailbox"), anchor.toUtf8()));
        HttpTransport::Response http;
        if (!transport.send("POST", config.url, headers, Ews::envelope(header, body), operation, &http)) return false;
        if (http.status != 200 && http.status != 500) {
            transport.fail(Failure::Rejected, u"EWS %1 returned HTTP %2"_s.arg(operation).arg(http.status));
            return false;
        }
        QString error;
        if (!Ews::parse(http.body, response, &error)) {
            transport.fail(Failure::Rejected, u"EWS %1: %2"_s.arg(operation, error));
            return false;
        }
        int backOff = response->backOffMs;
        bool busy = response->faultCode == "ErrorServerBusy"_L1;
        for (const Ews::ResponseMessage &message : std::as_const(response->messages)) {
            if (message.code != "ErrorServerBusy"_L1) continue;
            busy = true;
            backOff = qMax(backOff, message.backOffMs);
        }
        if (busy) {
            transport.fail(Failure::Offline, u"Exchange is throttling requests; retry later"_s,
                           backOff >= 0 ? int(std::ceil(backOff / 1000.0)) : -1);
            return false;
        }
        if (!response->fault.isEmpty()) {
            transport.fail(Failure::Rejected, u"Exchange refused %1: %2"_s.arg(operation, response->fault));
            return false;
        }
        return true;
    }

    // The first response message, which must succeed (else the transport fails).
    const Ews::ResponseMessage *success(const Ews::Response &response, const QString &operation)
    {
        if (response.messages.isEmpty()) {
            transport.fail(Failure::Rejected, u"EWS %1 answered nothing"_s.arg(operation));
            return nullptr;
        }
        const Ews::ResponseMessage &message = response.messages.first();
        if (!message.success() && message.responseClass != "Warning"_L1) {
            transport.fail(Failure::Rejected, u"Exchange refused %1: %2 %3"_s.arg(operation, message.code, message.text));
            return nullptr;
        }
        return &message;
    }

    void mailbox(QXmlStreamWriter &w) const
    {
        Ews::startT(w, u"Mailbox"_s);
        Ews::textT(w, u"Address"_s, config.mailbox.isEmpty() ? config.userName : config.mailbox);
        w.writeEndElement();
    }

    const Config &config;
    HttpTransport transport;
    Ews::Header header;
};

void constant(QXmlStreamWriter &w, const QString &fieldUri, const QString &comparison, const QString &value)
{
    Ews::startT(w, comparison);
    Ews::fieldUri(w, fieldUri);
    Ews::startT(w, u"FieldURIOrConstant"_s);
    Ews::startT(w, u"Constant"_s);
    w.writeAttribute(u"Value"_s, value);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndElement();
}

void distinguished(QXmlStreamWriter &w, const QString &id)
{
    Ews::startT(w, u"DistinguishedFolderId"_s);
    w.writeAttribute(u"Id"_s, id);
    w.writeEndElement();
}

QVariantMap repliesFromXml(const XmlNode *settings)
{
    if (settings == nullptr) return {};
    const QString state = settings->childText(u"OofState");
    const QString audience = settings->childText(u"ExternalAudience");
    const XmlNode *duration = settings->child(u"Duration");
    const auto when = [duration](QStringView name) {
        const QDateTime time = QDateTime::fromString(duration ? duration->childText(name) : QString(), Qt::ISODate);
        return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
    };
    const auto message = [settings](QStringView reply) {
        const XmlNode *node = settings->child(reply);
        return textFromHtml(node ? node->childText(u"Message") : QString());
    };
    return {{u"enabled"_s, state == "Enabled"_L1 || state == "Scheduled"_L1},
            {u"scheduled"_s, state == "Scheduled"_L1},
            {u"start"_s, when(u"StartTime")},
            {u"end"_s, when(u"EndTime")},
            {u"internalMessage"_s, message(u"InternalReply")},
            {u"externalMessage"_s, message(u"ExternalReply")},
            {u"externalAudience"_s, audience == "Known"_L1 ? u"contacts"_s : audience == "None"_L1 ? u"none"_s : u"all"_s}};
}

QString words(const XmlNode &node)
{
    QList<const XmlNode *> leaves;
    node.findAll(u"String", &leaves);
    node.findAll(u"EmailAddress", &leaves);
    QStringList out;
    for (const XmlNode *leaf : std::as_const(leaves)) out.append(leaf->text.trimmed());
    return out.join(u", "_s);
}

QString ruleSummary(const XmlNode &rule)
{
    static const QList<QPair<const char *, const char *>> conditionWords{
        {"FromAddresses", "from %1"}, {"SentToAddresses", "sent to %1"}, {"ContainsSubjectStrings", "subject contains %1"},
        {"ContainsBodyStrings", "body contains %1"}, {"ContainsSubjectOrBodyStrings", "subject or body contains %1"},
        {"ContainsHeaderStrings", "header contains %1"}, {"ContainsSenderStrings", "sender contains %1"},
        {"Categories", "assigned to %1"}};
    QStringList conditions;
    if (const XmlNode *when = rule.child(u"Conditions")) {
        for (const auto &[name, text] : conditionWords)
            if (const XmlNode *c = when->child(QString::fromLatin1(name))) conditions.append(QString::fromLatin1(text).arg(words(*c)));
        if (when->childText(u"SentOnlyToMe") == "true"_L1) conditions.append(u"sent only to me"_s);
        if (when->childText(u"HasAttachments") == "true"_L1) conditions.append(u"with an attachment"_s);
        if (!when->childText(u"Importance").isEmpty()) conditions.append(u"importance %1"_s.arg(when->childText(u"Importance").toLower()));
    }
    static const QList<QPair<const char *, const char *>> actionWords{
        {"MoveToFolder", "move to a folder"}, {"CopyToFolder", "copy to a folder"}, {"Delete", "delete"},
        {"PermanentDelete", "delete permanently"}, {"MarkAsRead", "mark as read"}, {"AssignCategories", "assign categories"},
        {"ForwardToRecipients", "forward"}, {"RedirectToRecipients", "redirect"}, {"MarkImportance", "set importance"},
        {"StopProcessingRules", "stop processing more rules"}};
    QStringList actions;
    if (const XmlNode *act = rule.child(u"Actions")) {
        for (const auto &[name, text] : actionWords) {
            const XmlNode *a = act->child(QString::fromLatin1(name));
            if (a != nullptr && a->text.trimmed() != "false"_L1) actions.append(QString::fromLatin1(text));
        }
    }
    return (conditions.isEmpty() ? u"every message"_s : conditions.join(u" and "_s)) + u" → "_s
        + (actions.isEmpty() ? u"(no action QindaMail can show)"_s : actions.join(u", "_s));
}

// The folder to look in: the inbox, or the folder whose display name is
// the IMAP mailbox's last segment. Empty with the transport failed or
// nothing found (`*missing`).
QString folderFor(EwsCall &ews, const QString &mailbox, bool *missing)
{
    const QString leaf = mailbox.section(QRegularExpression(u"[/.]"_s), -1).trimmed();
    if (mailbox.isEmpty() || mailbox.compare(u"INBOX"_s, Qt::CaseInsensitive) == 0) return u"inbox"_s;
    Ews::Response response;
    const bool ok = ews.call(u"FindFolder"_s, [&](QXmlStreamWriter &w) {
        Ews::startM(w, u"FindFolder"_s);
        w.writeAttribute(u"Traversal"_s, u"Deep"_s);
        Ews::startM(w, u"FolderShape"_s);
        Ews::textT(w, u"BaseShape"_s, u"IdOnly"_s);
        w.writeEndElement();
        Ews::startM(w, u"Restriction"_s);
        constant(w, u"folder:DisplayName"_s, u"IsEqualTo"_s, leaf);
        w.writeEndElement();
        Ews::startM(w, u"ParentFolderIds"_s);
        distinguished(w, u"msgfolderroot"_s);
        w.writeEndElement();
        w.writeEndElement();
    }, &response);
    const Ews::ResponseMessage *message = ok ? ews.success(response, u"FindFolder"_s) : nullptr;
    const XmlNode *id = message != nullptr && message->node ? message->node->find(u"FolderId") : nullptr;
    *missing = message != nullptr && id == nullptr;
    return id != nullptr ? u"id:"_s + id->attribute(u"Id"_s) : QString();
}

void writeTags(QXmlStreamWriter &w, const QVariantMap &input, const QString &itemElement)
{
    if (input.contains(u"categories"_s)) {
        const QStringList categories = input.value(u"categories"_s).toStringList();
        if (categories.isEmpty()) {
            Ews::startT(w, u"DeleteItemField"_s);
            Ews::fieldUri(w, u"item:Categories"_s);
            w.writeEndElement();
        } else {
            Ews::startT(w, u"SetItemField"_s);
            Ews::fieldUri(w, u"item:Categories"_s);
            Ews::startT(w, itemElement);
            Ews::startT(w, u"Categories"_s);
            for (const QString &name : categories) Ews::textT(w, u"String"_s, name);
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
        }
    }
    if (!input.contains(u"flag"_s) || itemElement != "Message"_L1) return;
    const QVariantMap flag = input.value(u"flag"_s).toMap();
    const QString status = flag.value(u"status"_s).toString();
    QTimeZone zone(input.value(u"zone"_s).toString().toUtf8());
    if (!zone.isValid()) zone = QTimeZone(QTimeZone::UTC);
    const auto day = [&zone](const QString &date) {
        return utc(QDateTime(QDate::fromString(date, Qt::ISODate), QTime(0, 0), zone));
    };
    Ews::startT(w, u"SetItemField"_s);
    Ews::fieldUri(w, u"item:Flag"_s);
    Ews::startT(w, u"Message"_s);
    Ews::startT(w, u"Flag"_s);
    Ews::textT(w, u"FlagStatus"_s, status == "flagged"_L1 ? u"Flagged"_s : status == "complete"_L1 ? u"Complete"_s : u"NotFlagged"_s);
    const QString start = flag.value(u"start"_s).toString(), due = flag.value(u"due"_s).toString();
    if (status == "flagged"_L1 && !due.isEmpty()) {
        Ews::textT(w, u"StartDate"_s, day(start.isEmpty() ? due : start));
        Ews::textT(w, u"DueDate"_s, day(due));
    }
    if (status == "complete"_L1 && !flag.value(u"completed"_s).toString().isEmpty())
        Ews::textT(w, u"CompleteDate"_s, utc(QDateTime::fromString(flag.value(u"completed"_s).toString(), Qt::ISODate)));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndElement();
}

QVariantMap flagFromXml(const XmlNode *flag)
{
    if (flag == nullptr) return {{u"status"_s, QString()}};
    const QString status = flag->childText(u"FlagStatus");
    const auto day = [flag](QStringView name) {
        const QDateTime time = QDateTime::fromString(flag->childText(name), Qt::ISODate);
        return time.isValid() ? time.toLocalTime().date().toString(Qt::ISODate) : QString();
    };
    const QDateTime completed = QDateTime::fromString(flag->childText(u"CompleteDate"), Qt::ISODate);
    return {{u"status"_s, status == "Flagged"_L1 ? u"flagged"_s : status == "Complete"_L1 ? u"complete"_s : QString()},
            {u"start"_s, day(u"StartDate")}, {u"due"_s, day(u"DueDate")},
            {u"completed"_s, completed.isValid() ? completed.toUTC().toString(Qt::ISODate) : QString()}};
}

} // namespace

QVariantMap ews(Op op, const Config &config, const QVariantMap &input)
{
    EwsCall ews(config);
    QVariantMap out;
    QString problem;
    Ews::Response response;
    switch (op) {
    case Op::ReadAutoReplies:
        if (ews.call(u"GetUserOofSettings"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"GetUserOofSettingsRequest"_s);
                ews.mailbox(w);
                w.writeEndElement();
            }, &response) && ews.success(response, u"GetUserOofSettings"_s))
            out.insert(u"settings"_s, repliesFromXml(response.root.find(u"OofSettings")));
        break;
    case Op::WriteAutoReplies: {
        const bool enabled = input.value(u"enabled"_s).toBool(), scheduled = input.value(u"scheduled"_s).toBool();
        const QString audience = input.value(u"externalAudience"_s).toString();
        if (ews.call(u"SetUserOofSettings"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"SetUserOofSettingsRequest"_s);
                ews.mailbox(w);
                Ews::startT(w, u"UserOofSettings"_s);
                Ews::textT(w, u"OofState"_s, !enabled ? u"Disabled"_s : scheduled ? u"Scheduled"_s : u"Enabled"_s);
                Ews::textT(w, u"ExternalAudience"_s, audience == "contacts"_L1 ? u"Known"_s : audience == "none"_L1 ? u"None"_s : u"All"_s);
                if (scheduled) {
                    Ews::startT(w, u"Duration"_s);
                    Ews::textT(w, u"StartTime"_s, utc(QDateTime::fromString(input.value(u"start"_s).toString(), Qt::ISODate)));
                    Ews::textT(w, u"EndTime"_s, utc(QDateTime::fromString(input.value(u"end"_s).toString(), Qt::ISODate)));
                    w.writeEndElement();
                }
                for (const auto &[element, key] : {std::pair{u"InternalReply"_s, u"internalMessage"_s},
                                                   std::pair{u"ExternalReply"_s, u"externalMessage"_s}}) {
                    Ews::startT(w, element);
                    Ews::textT(w, u"Message"_s, htmlFromText(input.value(key).toString()));
                    w.writeEndElement();
                }
                w.writeEndElement();
                w.writeEndElement();
            }, &response) && ews.success(response, u"SetUserOofSettings"_s))
            out.insert(u"settings"_s, input);
        break;
    }
    case Op::AddCategory:
        problem = u"Exchange keeps its category list in Outlook; QindaMail keeps this category on this computer"_s;
        break;
    case Op::ListCategories: {
        if (!ews.call(u"GetUserConfiguration"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"GetUserConfiguration"_s);
                Ews::startM(w, u"UserConfigurationName"_s);
                w.writeAttribute(u"Name"_s, u"CategoryList"_s);
                distinguished(w, u"calendar"_s);
                w.writeEndElement();
                Ews::startM(w, u"UserConfigurationProperties"_s);
                w.writeCharacters(u"XmlData"_s);
                w.writeEndElement();
                w.writeEndElement();
            }, &response))
            break;
        QVariantList categories;
        const Ews::ResponseMessage *first = response.messages.isEmpty() ? nullptr : &response.messages.first();
        if (first != nullptr && first->code == "ErrorItemNotFound"_L1) {
            out.insert(u"categories"_s, categories);      // never customised: Outlook's defaults apply
            break;
        }
        if (ews.success(response, u"GetUserConfiguration"_s) == nullptr) break;
        const XmlNode *data = response.root.find(u"XmlData");
        XmlNode list;
        QString error;
        if (data != nullptr && parseXml(QByteArray::fromBase64(data->text.trimmed().toLatin1()), &list, &error)) {
            for (const XmlNode &node : list.children) {
                if (node.name != "category"_L1) continue;
                categories.append(QVariantMap{{u"name"_s, node.attribute(u"name"_s)},
                                              {u"preset"_s, node.attributes.value(u"color"_s, u"-1"_s).toInt()},
                                              {u"id"_s, node.attribute(u"guid"_s)}});
            }
        }
        out.insert(u"categories"_s, categories);
        break;
    }
    case Op::ListRules: {
        if (!ews.call(u"GetInboxRules"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"GetInboxRules"_s);
                w.writeTextElement(Ews::kMessagesNs, u"MailboxSmtpAddress"_s, config.mailbox.isEmpty() ? config.userName : config.mailbox);
                w.writeEndElement();
            }, &response) || ews.success(response, u"GetInboxRules"_s) == nullptr)
            break;
        QVariantList rules;
        QList<const XmlNode *> nodes;
        response.root.findAll(u"Rule", &nodes);
        for (const XmlNode *rule : std::as_const(nodes))
            rules.append(QVariantMap{{u"name"_s, rule->childText(u"DisplayName")},
                                     {u"enabled"_s, rule->childText(u"IsEnabled") != "false"_L1},
                                     {u"order"_s, rule->childText(u"Priority").toInt()},
                                     {u"summary"_s, ruleSummary(*rule)}});
        out.insert(u"rules"_s, rules);
        break;
    }
    case Op::SetItemState: {
        const bool event = input.value(u"kind"_s).toString() == "event"_L1;
        QString itemId = input.value(u"remoteId"_s).toString(), changeKey;
        if (!event) {
            bool missing = false;
            const QString folder = folderFor(ews, input.value(u"folder"_s).toString(), &missing);
            if (folder.isEmpty()) {
                if (missing) out.insert(u"found"_s, false);
                break;
            }
            if (!ews.call(u"FindItem"_s, [&](QXmlStreamWriter &w) {
                    Ews::startM(w, u"FindItem"_s);
                    w.writeAttribute(u"Traversal"_s, u"Shallow"_s);
                    Ews::startM(w, u"ItemShape"_s);
                    Ews::textT(w, u"BaseShape"_s, u"IdOnly"_s);
                    w.writeEndElement();
                    Ews::startM(w, u"Restriction"_s);
                    constant(w, u"message:InternetMessageId"_s, u"IsEqualTo"_s,
                             u'<' + input.value(u"internetMessageId"_s).toString().trimmed() + u'>');
                    w.writeEndElement();
                    Ews::startM(w, u"ParentFolderIds"_s);
                    if (folder.startsWith("id:"_L1)) {
                        Ews::startT(w, u"FolderId"_s);
                        w.writeAttribute(u"Id"_s, folder.mid(3));
                        w.writeEndElement();
                    } else {
                        distinguished(w, folder);
                    }
                    w.writeEndElement();
                    w.writeEndElement();
                }, &response))
                break;
            const Ews::ResponseMessage *found = ews.success(response, u"FindItem"_s);
            const XmlNode *id = found != nullptr && found->node ? found->node->find(u"ItemId") : nullptr;
            out.insert(u"found"_s, id != nullptr);
            if (id == nullptr) break;
            itemId = id->attribute(u"Id"_s);
            changeKey = id->attribute(u"ChangeKey"_s);
        }
        if (!ews.call(u"UpdateItem"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"UpdateItem"_s);
                w.writeAttribute(u"ConflictResolution"_s, u"AlwaysOverwrite"_s);
                w.writeAttribute(u"MessageDisposition"_s, u"SaveOnly"_s);
                if (event) w.writeAttribute(u"SendMeetingInvitationsOrCancellations"_s, u"SendToNone"_s);
                Ews::startM(w, u"ItemChanges"_s);
                Ews::startT(w, u"ItemChange"_s);
                Ews::startT(w, u"ItemId"_s);
                w.writeAttribute(u"Id"_s, itemId);
                if (!changeKey.isEmpty()) w.writeAttribute(u"ChangeKey"_s, changeKey);
                w.writeEndElement();
                Ews::startT(w, u"Updates"_s);
                writeTags(w, input, event ? u"CalendarItem"_s : u"Message"_s);
                w.writeEndElement();
                w.writeEndElement();
                w.writeEndElement();
                w.writeEndElement();
            }, &response) || ews.success(response, u"UpdateItem"_s) == nullptr)
            break;
        out.insert(u"found"_s, true);
        out.insert(u"remoteId"_s, itemId);
        break;
    }
    case Op::PullItemStates: {
        const QDateTime since = QDateTime::currentDateTimeUtc().addDays(-input.value(u"days"_s, 30).toInt());
        if (!ews.call(u"FindItem"_s, [&](QXmlStreamWriter &w) {
                Ews::startM(w, u"FindItem"_s);
                w.writeAttribute(u"Traversal"_s, u"Shallow"_s);
                Ews::itemShape(w, u"IdOnly"_s, {u"message:InternetMessageId"_s, u"item:Categories"_s, u"item:Flag"_s});
                Ews::startM(w, u"IndexedPageItemView"_s);
                w.writeAttribute(u"MaxEntriesReturned"_s, u"1000"_s);
                w.writeAttribute(u"Offset"_s, u"0"_s);
                w.writeAttribute(u"BasePoint"_s, u"Beginning"_s);
                w.writeEndElement();
                Ews::startM(w, u"Restriction"_s);
                constant(w, u"item:DateTimeReceived"_s, u"IsGreaterThanOrEqualTo"_s, utc(since));
                w.writeEndElement();
                Ews::startM(w, u"ParentFolderIds"_s);
                distinguished(w, u"inbox"_s);
                w.writeEndElement();
                w.writeEndElement();
            }, &response) || ews.success(response, u"FindItem"_s) == nullptr)
            break;
        QVariantList items;
        QList<const XmlNode *> messages;
        response.root.findAll(u"Message", &messages);
        for (const XmlNode *message : std::as_const(messages)) {
            QString id = message->childText(u"InternetMessageId");
            if (id.startsWith(u'<') && id.endsWith(u'>')) id = id.mid(1, id.size() - 2);
            if (id.isEmpty()) continue;
            QStringList categories;
            if (const XmlNode *list = message->child(u"Categories"))
                for (const XmlNode &value : list->children) categories.append(value.text.trimmed());
            items.append(QVariantMap{{u"internetMessageId"_s, id}, {u"categories"_s, categories},
                                     {u"flag"_s, flagFromXml(message->child(u"Flag"))}});
        }
        out.insert(u"items"_s, items);
        break;
    }
    }
    return finish(ews.transport, out, problem);
}

} // namespace QindaOffice::Mail::MailboxJobs
