// SPDX-License-Identifier: GPL-3.0-or-later
// EwsFakeServer's mailbox side (M81, D-090), shaped after the request and
// response examples of the EWS reference: GetUserOofSettings and
// SetUserOofSettings (one ResponseMessage, no ResponseMessages wrapper),
// GetUserConfiguration (CategoryList, XmlData in base64), GetInboxRules
// (the response IS the message), FindFolder over mail folders, FindItem
// over messages, and UpdateItem of categories and flags.
#include "ews_fake_server.h"

#include <QTimeZone>

#include <functional>
#include <utility>

using QindaOffice::Mail::XmlNode;

namespace {
const QString S = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
const QString T = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/types");
const QString M = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/messages");

QString utc(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")); }

// The envelope around `inner`, which writes the operation's response element.
FakeHttpServer::Reply envelope(const std::function<void(QXmlStreamWriter &)> &inner)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeNamespace(S, QStringLiteral("s"));
    w.writeNamespace(T, QStringLiteral("t"));
    w.writeNamespace(M, QStringLiteral("m"));
    w.writeStartDocument();
    w.writeStartElement(S, QStringLiteral("Envelope"));
    w.writeStartElement(S, QStringLiteral("Body"));
    inner(w);
    w.writeEndDocument();
    return {200, {{"Content-Type", "text/xml; charset=utf-8"}}, out};
}

void status(QXmlStreamWriter &w, const QString &code, bool ownElement = true)
{
    if (ownElement) w.writeStartElement(M, QStringLiteral("ResponseMessage"));
    w.writeAttribute(QStringLiteral("ResponseClass"), code == QLatin1String("NoError") ? QStringLiteral("Success") : QStringLiteral("Error"));
    w.writeTextElement(M, QStringLiteral("ResponseCode"), code);
    if (ownElement) w.writeEndElement();
}

// The id a FindItem/FindFolder ParentFolderIds names ("inbox", a FolderId).
QString parentOf(const XmlNode &operation)
{
    const XmlNode *parents = operation.child(u"ParentFolderIds");
    if (parents == nullptr || parents->children.isEmpty()) return {};
    return parents->children.first().attribute(QStringLiteral("Id"));
}

QString constantOf(const XmlNode &operation)
{
    const XmlNode *constant = operation.find(u"Constant");
    return constant ? constant->attribute(QStringLiteral("Value")) : QString();
}
} // namespace

bool EwsFakeServer::mailboxOperation(const XmlNode &operation, FakeHttpServer::Reply *reply)
{
    const QString &name = operation.name;
    if (name == u"GetUserOofSettingsRequest") {
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("GetUserOofSettingsResponse"));
            status(w, QStringLiteral("NoError"));
            w.writeStartElement(T, QStringLiteral("OofSettings"));
            w.writeTextElement(T, QStringLiteral("OofState"), oofState);
            w.writeTextElement(T, QStringLiteral("ExternalAudience"), oofAudience);
            w.writeStartElement(T, QStringLiteral("Duration"));
            w.writeTextElement(T, QStringLiteral("StartTime"), utc(oofStart.isValid() ? oofStart : QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC)));
            w.writeTextElement(T, QStringLiteral("EndTime"), utc(oofEnd.isValid() ? oofEnd : QDateTime(QDate(2026, 1, 2), QTime(0, 0), QTimeZone::UTC)));
            w.writeEndElement();
            for (const auto &[element, message] : {std::pair{QStringLiteral("InternalReply"), oofInternal},
                                                   std::pair{QStringLiteral("ExternalReply"), oofExternal}}) {
                w.writeStartElement(T, element);
                w.writeTextElement(T, QStringLiteral("Message"), message);
                w.writeEndElement();
            }
            w.writeEndElement();
            w.writeTextElement(M, QStringLiteral("AllowExternalOof"), QStringLiteral("All"));
            w.writeEndElement();
        });
        return true;
    }
    if (name == u"SetUserOofSettingsRequest") {
        const XmlNode *settings = operation.child(u"UserOofSettings");
        if (settings != nullptr) {
            oofState = settings->childText(u"OofState");
            oofAudience = settings->childText(u"ExternalAudience");
            if (const XmlNode *duration = settings->child(u"Duration")) {
                oofStart = QDateTime::fromString(duration->childText(u"StartTime"), Qt::ISODate);
                oofEnd = QDateTime::fromString(duration->childText(u"EndTime"), Qt::ISODate);
            }
            const XmlNode *internal = settings->child(u"InternalReply");
            const XmlNode *external = settings->child(u"ExternalReply");
            oofInternal = internal ? internal->childText(u"Message") : QString();
            oofExternal = external ? external->childText(u"Message") : QString();
        }
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("SetUserOofSettingsResponse"));
            status(w, settings != nullptr ? QStringLiteral("NoError") : QStringLiteral("ErrorInvalidRequest"));
            w.writeEndElement();
        });
        return true;
    }
    if (name == u"GetUserConfiguration") {
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("GetUserConfigurationResponse"));
            w.writeStartElement(M, QStringLiteral("ResponseMessages"));
            w.writeStartElement(M, QStringLiteral("GetUserConfigurationResponseMessage"));
            status(w, categoryList.isEmpty() ? QStringLiteral("ErrorItemNotFound") : QStringLiteral("NoError"), false);
            if (!categoryList.isEmpty()) {
                w.writeStartElement(M, QStringLiteral("UserConfiguration"));
                w.writeTextElement(T, QStringLiteral("XmlData"), QString::fromLatin1(categoryList.toBase64()));
                w.writeEndElement();
            }
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
        });
        return true;
    }
    if (name == u"GetInboxRules") {
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("GetInboxRulesResponse"));
            status(w, QStringLiteral("NoError"), false);
            w.writeTextElement(M, QStringLiteral("OutlookRuleBlobExists"), QStringLiteral("true"));
            w.writeStartElement(M, QStringLiteral("InboxRules"));
            w.writeEndElement();
            w.writeEndElement();
        });
        // The rules go in as raw t: elements (a test's own XML).
        reply->body.replace("<m:InboxRules></m:InboxRules>", "<m:InboxRules>" + inboxRules + "</m:InboxRules>");
        reply->body.replace("<m:InboxRules/>", "<m:InboxRules>" + inboxRules + "</m:InboxRules>");
        return true;
    }
    if (name == u"FindFolder" && parentOf(operation) == QLatin1String("msgfolderroot")) {
        const QString wanted = constantOf(operation);
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("FindFolderResponse"));
            w.writeStartElement(M, QStringLiteral("ResponseMessages"));
            w.writeStartElement(M, QStringLiteral("FindFolderResponseMessage"));
            status(w, QStringLiteral("NoError"), false);
            w.writeStartElement(M, QStringLiteral("RootFolder"));
            w.writeStartElement(T, QStringLiteral("Folders"));
            for (auto it = mailFolders.cbegin(); it != mailFolders.cend(); ++it) {
                if (it.value() != wanted) continue;
                w.writeStartElement(T, QStringLiteral("Folder"));
                w.writeStartElement(T, QStringLiteral("FolderId"));
                w.writeAttribute(QStringLiteral("Id"), it.key());
                w.writeEndElement();
                w.writeEndElement();
            }
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
        });
        return true;
    }
    const QString parent = parentOf(operation);
    if (name == u"FindItem" && (parent == QLatin1String("inbox") || mailFolders.contains(parent))) {
        const bool byId = operation.find(u"IsEqualTo") != nullptr;
        const QString value = constantOf(operation);
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("FindItemResponse"));
            w.writeStartElement(M, QStringLiteral("ResponseMessages"));
            w.writeStartElement(M, QStringLiteral("FindItemResponseMessage"));
            status(w, QStringLiteral("NoError"), false);
            w.writeStartElement(M, QStringLiteral("RootFolder"));
            w.writeStartElement(T, QStringLiteral("Items"));
            for (auto it = mail.cbegin(); it != mail.cend(); ++it) {
                if (it->folderId != parent) continue;
                if (byId ? it->internetMessageId != value : it->received < QDateTime::fromString(value, Qt::ISODate)) continue;
                w.writeStartElement(T, QStringLiteral("Message"));
                w.writeStartElement(T, QStringLiteral("ItemId"));
                w.writeAttribute(QStringLiteral("Id"), it.key());
                w.writeAttribute(QStringLiteral("ChangeKey"), QStringLiteral("MCK%1").arg(it->changeKey));
                w.writeEndElement();
                if (!byId) {
                    if (!it->categories.isEmpty()) {
                        w.writeStartElement(T, QStringLiteral("Categories"));
                        for (const QString &category : it->categories) w.writeTextElement(T, QStringLiteral("String"), category);
                        w.writeEndElement();
                    }
                    w.writeTextElement(T, QStringLiteral("InternetMessageId"), it->internetMessageId);
                    w.writeStartElement(T, QStringLiteral("Flag"));
                    w.writeTextElement(T, QStringLiteral("FlagStatus"), it->flagStatus);
                    if (it->start.isValid()) w.writeTextElement(T, QStringLiteral("StartDate"), utc(it->start));
                    if (it->due.isValid()) w.writeTextElement(T, QStringLiteral("DueDate"), utc(it->due));
                    if (it->completed.isValid()) w.writeTextElement(T, QStringLiteral("CompleteDate"), utc(it->completed));
                    w.writeEndElement();
                }
                w.writeEndElement();
            }
            for (int i = 0; i < 5; ++i) w.writeEndElement();
        });
        return true;
    }
    if (name == u"UpdateItem") {
        const XmlNode *id = operation.find(u"ItemId");
        const QString itemId = id ? id->attribute(QStringLiteral("Id")) : QString();
        const XmlNode *updates = operation.find(u"Updates");
        bool tags = false;
        QList<const XmlNode *> uris;
        if (updates != nullptr) updates->findAll(u"FieldURI", &uris);
        for (const XmlNode *uri : std::as_const(uris)) {
            const QString field = uri->attribute(QStringLiteral("FieldURI"));
            tags = tags || field == QLatin1String("item:Categories") || field == QLatin1String("item:Flag");
        }
        if (!tags || (!mail.contains(itemId) && !items.contains(itemId))) return false;   // the calendar's own path
        lastUpdateSend = operation.attribute(QStringLiteral("SendMeetingInvitationsOrCancellations"));
        QStringList categories;
        bool clear = false;
        for (const XmlNode &change : updates->children) {
            const XmlNode *uri = change.child(u"FieldURI");
            const QString field = uri ? uri->attribute(QStringLiteral("FieldURI")) : QString();
            if (field == QLatin1String("item:Categories")) {
                clear = change.name == u"DeleteItemField";
                if (const XmlNode *list = change.find(u"Categories"))
                    for (const XmlNode &value : list->children) categories.append(value.text.trimmed());
            } else if (field == QLatin1String("item:Flag") && mail.contains(itemId)) {
                const XmlNode *flag = change.find(u"Flag");
                MailItem &message = mail[itemId];
                message.flagStatus = flag ? flag->childText(u"FlagStatus") : QStringLiteral("NotFlagged");
                message.start = QDateTime::fromString(flag ? flag->childText(u"StartDate") : QString(), Qt::ISODate);
                message.due = QDateTime::fromString(flag ? flag->childText(u"DueDate") : QString(), Qt::ISODate);
                message.completed = QDateTime::fromString(flag ? flag->childText(u"CompleteDate") : QString(), Qt::ISODate);
            }
        }
        const bool touchesCategories = clear || !categories.isEmpty();
        if (mail.contains(itemId)) {
            if (touchesCategories) mail[itemId].categories = categories;
            ++mail[itemId].changeKey;
        } else {
            if (touchesCategories) items[itemId].categories = categories;
            bump(itemId);
        }
        *reply = envelope([&](QXmlStreamWriter &w) {
            w.writeStartElement(M, QStringLiteral("UpdateItemResponse"));
            w.writeStartElement(M, QStringLiteral("ResponseMessages"));
            w.writeStartElement(M, QStringLiteral("UpdateItemResponseMessage"));
            status(w, QStringLiteral("NoError"), false);
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
        });
        return true;
    }
    return false;
}
