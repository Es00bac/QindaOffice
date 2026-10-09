// SPDX-License-Identifier: GPL-3.0-or-later
#include "ews_fake_server.h"

using QindaOffice::Mail::XmlNode;

namespace {
const QString S = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
const QString T = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/types");
const QString M = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/messages");

QString utc(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")); }
QDateTime instant(const QString &text) { return QDateTime::fromString(text.trimmed(), Qt::ISODate).toUTC(); }

void text(QXmlStreamWriter &w, const QString &name, const QString &value) { w.writeTextElement(T, name, value); }

void itemId(QXmlStreamWriter &w, const QString &id, const QString &changeKey = {})
{
    w.writeStartElement(T, QStringLiteral("ItemId"));
    w.writeAttribute(QStringLiteral("Id"), id);
    if (!changeKey.isEmpty()) w.writeAttribute(QStringLiteral("ChangeKey"), changeKey);
    w.writeEndElement();
}

// Writes a parsed request node back (the stored Recurrence element).
void writeNode(QXmlStreamWriter &w, const XmlNode &node)
{
    w.writeStartElement(T, node.name);
    for (auto it = node.attributes.cbegin(); it != node.attributes.cend(); ++it) w.writeAttribute(it.key(), it.value());
    if (node.children.isEmpty()) w.writeCharacters(node.text.trimmed());
    for (const XmlNode &child : node.children) writeNode(w, child);
    w.writeEndElement();
}

void message(QXmlStreamWriter &w, const QString &name, const QString &code, const std::function<void()> &inner = {})
{
    w.writeStartElement(M, name);
    const bool ok = code == QLatin1String("NoError");
    w.writeAttribute(QStringLiteral("ResponseClass"), ok ? QStringLiteral("Success") : QStringLiteral("Error"));
    if (!ok) w.writeTextElement(M, QStringLiteral("MessageText"), QStringLiteral("fake server: ") + code);
    w.writeTextElement(M, QStringLiteral("ResponseCode"), code);
    if (inner) inner();
    w.writeEndElement();
}

void mailbox(QXmlStreamWriter &w, const QString &entry)
{
    w.writeStartElement(T, QStringLiteral("Mailbox"));
    text(w, QStringLiteral("Name"), entry.section(QLatin1Char('<'), 0, 0).trimmed());
    text(w, QStringLiteral("EmailAddress"), entry.section(QLatin1Char('<'), 1).chopped(1));
    w.writeEndElement();
}
} // namespace

EwsFakeServer::EwsFakeServer() : http([this](const FakeHttpServer::Request &request) { return handle(request); }) {}

QString EwsFakeServer::masterOfException(const QString &itemId, int *index) const
{
    for (auto it = items.cbegin(); it != items.cend(); ++it)
        for (int i = 0; i < it->modified.size(); ++i)
            if (it->modified.at(i).itemId == itemId) { *index = i; return it.key(); }
    return {};
}

FakeHttpServer::Reply EwsFakeServer::handle(const FakeHttpServer::Request &request)
{
    if (throttle) return {503, {{"Retry-After", "12"}}, {}};
    XmlNode root;
    QString error;
    if (!QindaOffice::Mail::parseXml(request.body, &root, &error) || !root.child(u"Body") || root.child(u"Body")->children.isEmpty())
        return {400, {}, error.toUtf8()};
    const XmlNode operation = root.child(u"Body")->children.first();
    operations.append(operation.name);
    lastOperation.insert(operation.name, operation);
    if (busy) {
        return {500, {{"Content-Type", "text/xml; charset=utf-8"}},
            "<s:Envelope xmlns:s=\"" + S.toUtf8() + "\"><s:Body><s:Fault><faultcode>a:ErrorServerBusy</faultcode>"
            "<faultstring>The server cannot service this request right now. Try again later.</faultstring><detail>"
            "<e:ResponseCode xmlns:e=\"http://schemas.microsoft.com/exchange/services/2006/errors\">ErrorServerBusy</e:ResponseCode>"
            "<t:MessageXml xmlns:t=\"" + T.toUtf8() + "\"><t:Value Name=\"BackOffMilliseconds\">30000</t:Value></t:MessageXml>"
            "</detail></s:Fault></s:Body></s:Envelope>"};
    }
    FakeHttpServer::Reply mailboxReply;
    if (mailboxOperation(operation, &mailboxReply)) return mailboxReply;
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeNamespace(S, QStringLiteral("s"));
    w.writeNamespace(T, QStringLiteral("t"));
    w.writeNamespace(M, QStringLiteral("m"));
    w.writeStartDocument();
    w.writeStartElement(S, QStringLiteral("Envelope"));
    w.writeStartElement(S, QStringLiteral("Body"));
    w.writeStartElement(M, operation.name + QStringLiteral("Response"));
    w.writeStartElement(M, QStringLiteral("ResponseMessages"));
    if (operation.name == u"FindItem") findItem(w);
    else if (operation.name == u"GetItem") getItem(w, operation);
    else if (operation.name == u"CreateItem") createItem(w, operation);
    else if (operation.name == u"UpdateItem") updateItem(w, operation);
    else if (operation.name == u"DeleteItem") deleteItem(w, operation);
    else folders(w, operation.name);
    w.writeEndDocument();
    return {200, {{"Content-Type", "text/xml; charset=utf-8"}}, out};
}

void EwsFakeServer::findItem(QXmlStreamWriter &w)
{
    message(w, QStringLiteral("FindItemResponseMessage"), QStringLiteral("NoError"), [&] {
        w.writeStartElement(M, QStringLiteral("RootFolder"));
        w.writeAttribute(QStringLiteral("TotalItemsInView"), QString::number(items.size()));
        w.writeAttribute(QStringLiteral("IncludesLastItemInRange"), QStringLiteral("true"));
        w.writeStartElement(T, QStringLiteral("Items"));
        for (const Item &item : std::as_const(items)) {
            const bool series = item.type == QLatin1String("RecurringMaster");
            w.writeStartElement(T, QStringLiteral("CalendarItem"));
            itemId(w, series ? QStringLiteral("occ:") + item.id : item.id, series ? QStringLiteral("OCK") : changeKey(item.id));
            text(w, QStringLiteral("CalendarItemType"), series ? QStringLiteral("Occurrence") : QStringLiteral("Single"));
            text(w, QStringLiteral("UID"), item.uid);
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
    });
}

void EwsFakeServer::writeItem(QXmlStreamWriter &w, const Item &item, bool full)
{
    w.writeStartElement(T, QStringLiteral("CalendarItem"));
    itemId(w, item.id, changeKey(item.id));
    if (!full) { w.writeEndElement(); return; }
    text(w, QStringLiteral("Subject"), item.subject);
    w.writeStartElement(T, QStringLiteral("Body"));
    w.writeAttribute(QStringLiteral("BodyType"), QStringLiteral("Text"));
    w.writeCharacters(item.body.isEmpty() ? QString() : item.body + QStringLiteral("\r\n"));
    w.writeEndElement();
    if (!item.categories.isEmpty()) {
        w.writeStartElement(T, QStringLiteral("Categories"));
        for (const QString &category : item.categories) text(w, QStringLiteral("String"), category);
        w.writeEndElement();
    }
    w.writeStartElement(T, QStringLiteral("EffectiveRights"));
    text(w, QStringLiteral("Modify"), item.canModify ? QStringLiteral("true") : QStringLiteral("false"));
    w.writeEndElement();
    text(w, QStringLiteral("Start"), utc(item.start));
    text(w, QStringLiteral("End"), utc(item.end));
    text(w, QStringLiteral("IsAllDayEvent"), item.allDay ? QStringLiteral("true") : QStringLiteral("false"));
    text(w, QStringLiteral("Location"), item.location);
    text(w, QStringLiteral("CalendarItemType"), item.type);
    if (!item.organizer.isEmpty()) {
        w.writeStartElement(T, QStringLiteral("Organizer"));
        mailbox(w, item.organizer);
        w.writeEndElement();
    }
    if (!item.attendees.isEmpty()) {
        w.writeStartElement(T, QStringLiteral("RequiredAttendees"));
        for (const QString &attendee : item.attendees) {
            w.writeStartElement(T, QStringLiteral("Attendee"));
            mailbox(w, attendee);
            text(w, QStringLiteral("ResponseType"), QStringLiteral("Accept"));
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!item.recurrence.name.isEmpty()) writeNode(w, item.recurrence);
    if (!item.modified.isEmpty()) {
        w.writeStartElement(T, QStringLiteral("ModifiedOccurrences"));
        for (const Occurrence &occurrence : item.modified) {
            w.writeStartElement(T, QStringLiteral("Occurrence"));
            itemId(w, occurrence.itemId, QStringLiteral("XCK"));
            text(w, QStringLiteral("Start"), utc(occurrence.start));
            text(w, QStringLiteral("End"), utc(occurrence.end));
            text(w, QStringLiteral("OriginalStart"), utc(occurrence.originalStart));
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!item.deleted.isEmpty()) {
        w.writeStartElement(T, QStringLiteral("DeletedOccurrences"));
        for (const QDateTime &start : item.deleted) {
            w.writeStartElement(T, QStringLiteral("DeletedOccurrence"));
            text(w, QStringLiteral("Start"), utc(start));
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (item.legacyZone) {
        w.writeStartElement(T, QStringLiteral("StartTimeZone"));
        w.writeAttribute(QStringLiteral("Id"), item.zone);
        w.writeAttribute(QStringLiteral("Name"), QStringLiteral("(UTC-07:00) Mountain Time (US & Canada)"));
        w.writeEndElement();
    }
    text(w, QStringLiteral("IsOnlineMeeting"), item.online ? QStringLiteral("true") : QStringLiteral("false"));
    if (!item.legacyZone) text(w, QStringLiteral("StartTimeZoneId"), item.zone);
    if (!item.joinUrl.isEmpty()) text(w, QStringLiteral("JoinOnlineMeetingUrl"), item.joinUrl);
    text(w, QStringLiteral("UID"), item.uid);
    w.writeEndElement();
}

void EwsFakeServer::writeOccurrenceItem(QXmlStreamWriter &w, const Occurrence &occurrence)
{
    w.writeStartElement(T, QStringLiteral("CalendarItem"));
    itemId(w, occurrence.itemId, QStringLiteral("XCK"));
    text(w, QStringLiteral("Subject"), occurrence.subject);
    w.writeStartElement(T, QStringLiteral("Body"));
    w.writeAttribute(QStringLiteral("BodyType"), QStringLiteral("Text"));
    w.writeCharacters(occurrence.body);
    w.writeEndElement();
    text(w, QStringLiteral("Start"), utc(occurrence.start));
    text(w, QStringLiteral("End"), utc(occurrence.end));
    text(w, QStringLiteral("OriginalStart"), utc(occurrence.originalStart));
    text(w, QStringLiteral("Location"), occurrence.location);
    text(w, QStringLiteral("IsCancelled"), occurrence.cancelled ? QStringLiteral("true") : QStringLiteral("false"));
    w.writeEndElement();
}

void EwsFakeServer::getItem(QXmlStreamWriter &w, const XmlNode &operation)
{
    const bool full = operation.find(u"AdditionalProperties") != nullptr;
    const XmlNode *ids = operation.child(u"ItemIds");
    for (const XmlNode &node : ids ? ids->children : QList<XmlNode>()) {
        QString id = node.attribute(QStringLiteral("Id"));
        if (node.name == u"RecurringMasterItemId") id = node.attribute(QStringLiteral("OccurrenceId")).mid(4);
        int index = -1;
        const QString master = node.name == u"ItemId" ? masterOfException(id, &index) : QString();
        if (items.contains(id)) {
            message(w, QStringLiteral("GetItemResponseMessage"), QStringLiteral("NoError"), [&] {
                w.writeStartElement(M, QStringLiteral("Items"));
                writeItem(w, items.value(id), full);
                w.writeEndElement();
            });
        } else if (!master.isEmpty()) {
            message(w, QStringLiteral("GetItemResponseMessage"), QStringLiteral("NoError"), [&] {
                w.writeStartElement(M, QStringLiteral("Items"));
                writeOccurrenceItem(w, items.value(master).modified.at(index));
                w.writeEndElement();
            });
        } else {
            message(w, QStringLiteral("GetItemResponseMessage"), QStringLiteral("ErrorItemNotFound"));
        }
    }
}

void EwsFakeServer::createItem(QXmlStreamWriter &w, const XmlNode &operation)
{
    const XmlNode *node = operation.find(u"CalendarItem");
    Item item;
    item.id = QStringLiteral("item-%1").arg(m_nextId);
    item.uid = QStringLiteral("uid-%1").arg(m_nextId++);
    item.subject = node->childText(u"Subject");
    item.body = node->child(u"Body") ? node->child(u"Body")->text : QString();
    item.start = instant(node->childText(u"Start"));
    item.end = instant(node->childText(u"End"));
    item.allDay = node->childText(u"IsAllDayEvent") == QLatin1String("true");
    item.location = node->childText(u"Location");
    item.zone = node->child(u"StartTimeZone") ? node->child(u"StartTimeZone")->attribute(QStringLiteral("Id")) : QString();
    if (const XmlNode *recurrence = node->child(u"Recurrence")) {
        item.recurrence = *recurrence;
        item.type = QStringLiteral("RecurringMaster");
    }
    items.insert(item.id, item);
    message(w, QStringLiteral("CreateItemResponseMessage"), QStringLiteral("NoError"), [&] {
        w.writeStartElement(M, QStringLiteral("Items"));
        writeItem(w, item, false);
        w.writeEndElement();
    });
}

void EwsFakeServer::apply(const XmlNode &updates, Item *item, Occurrence *occurrence)
{
    for (const XmlNode &change : updates.children) {
        const XmlNode *uri = change.child(u"FieldURI");
        const QString field = uri ? uri->attribute(QStringLiteral("FieldURI")) : QString();
        if (change.name == u"DeleteItemField") {
            if (item && field == QLatin1String("calendar:Recurrence")) { item->recurrence = {}; item->type = QStringLiteral("Single"); }
            continue;
        }
        const XmlNode *holder = change.child(u"CalendarItem");
        const XmlNode *value = holder && !holder->children.isEmpty() ? &holder->children.first() : nullptr;
        if (!value) continue;
        const QString v = value->text;
        if (field == QLatin1String("item:Subject")) (item ? item->subject : occurrence->subject) = v.trimmed();
        else if (field == QLatin1String("item:Body")) (item ? item->body : occurrence->body) = v;
        else if (field == QLatin1String("calendar:Location")) (item ? item->location : occurrence->location) = v.trimmed();
        else if (field == QLatin1String("calendar:End")) (item ? item->end : occurrence->end) = instant(v);
        else if (field == QLatin1String("calendar:Start")) {
            if (item && item->type == QLatin1String("RecurringMaster") && item->start != instant(v)) {
                item->modified.clear();            // Exchange drops exceptions when the series moves
                item->deleted.clear();
            }
            (item ? item->start : occurrence->start) = instant(v);
        } else if (item && field == QLatin1String("calendar:IsAllDayEvent")) item->allDay = v.trimmed() == QLatin1String("true");
        else if (item && field == QLatin1String("calendar:StartTimeZone")) item->zone = value->attribute(QStringLiteral("Id"));
        else if (item && field == QLatin1String("calendar:Recurrence")) { item->recurrence = *value; item->type = QStringLiteral("RecurringMaster"); }
    }
}

void EwsFakeServer::updateItem(QXmlStreamWriter &w, const XmlNode &operation)
{
    const XmlNode *changes = operation.child(u"ItemChanges");
    for (const XmlNode &change : changes ? changes->children : QList<XmlNode>()) {
        const XmlNode *updates = change.child(u"Updates");
        auto done = [&](const QString &id, const QString &key) {
            message(w, QStringLiteral("UpdateItemResponseMessage"), QStringLiteral("NoError"), [&] {
                w.writeStartElement(M, QStringLiteral("Items"));
                w.writeStartElement(T, QStringLiteral("CalendarItem"));
                itemId(w, id, key);
                w.writeEndElement();
                w.writeEndElement();
            });
        };
        if (const XmlNode *node = change.child(u"ItemId")) {
            const QString id = node->attribute(QStringLiteral("Id")), key = node->attribute(QStringLiteral("ChangeKey"));
            int index = -1;
            const QString master = masterOfException(id, &index);
            if (items.contains(id)) {
                if (!key.isEmpty() && key != changeKey(id)) { message(w, QStringLiteral("UpdateItemResponseMessage"), QStringLiteral("ErrorIrresolvableConflict")); continue; }
                apply(*updates, &items[id], nullptr);
                bump(id);
                done(id, changeKey(id));
            } else if (!master.isEmpty()) {
                apply(*updates, nullptr, &items[master].modified[index]);
                bump(master);
                done(id, QStringLiteral("XCK"));
            } else {
                message(w, QStringLiteral("UpdateItemResponseMessage"), QStringLiteral("ErrorItemNotFound"));
            }
        } else if (const XmlNode *node = change.child(u"OccurrenceItemId")) {
            const QString master = node->attribute(QStringLiteral("RecurringMasterId"));
            const int index = node->attribute(QStringLiteral("InstanceIndex")).toInt();
            Item &item = items[master];
            if (index < 1 || index > item.occurrenceStarts.size()) {
                message(w, QStringLiteral("UpdateItemResponseMessage"), QStringLiteral("ErrorCalendarOccurrenceIndexIsOutOfRecurrenceRange"));
                continue;
            }
            const QDateTime original = item.occurrenceStarts.at(index - 1);
            qsizetype at = -1;
            for (qsizetype i = 0; i < item.modified.size(); ++i)
                if (item.modified.at(i).originalStart == original) at = i;
            if (at < 0) {
                item.modified.append({QStringLiteral("exc-%1").arg(m_nextId++), original, original,
                                      original.addSecs(item.start.secsTo(item.end)), item.subject, item.location, item.body, false});
                at = item.modified.size() - 1;
            }
            apply(*updates, nullptr, &item.modified[at]);
            bump(master);
            done(item.modified.at(at).itemId, QStringLiteral("XCK"));
        }
    }
}

void EwsFakeServer::deleteItem(QXmlStreamWriter &w, const XmlNode &operation)
{
    const XmlNode *ids = operation.child(u"ItemIds");
    for (const XmlNode &node : ids ? ids->children : QList<XmlNode>()) {
        QString code = QStringLiteral("NoError");
        if (node.name == u"OccurrenceItemId") {
            const QString master = node.attribute(QStringLiteral("RecurringMasterId"));
            const int index = node.attribute(QStringLiteral("InstanceIndex")).toInt();
            if (!items.contains(master) || index < 1 || index > items[master].occurrenceStarts.size()) code = QStringLiteral("ErrorItemNotFound");
            else { items[master].deleted.append(items[master].occurrenceStarts.at(index - 1)); bump(master); }
        } else {
            const QString id = node.attribute(QStringLiteral("Id")), key = node.attribute(QStringLiteral("ChangeKey"));
            int index = -1;
            const QString master = masterOfException(id, &index);
            if (items.contains(id)) {
                if (!key.isEmpty() && key != changeKey(id)) code = QStringLiteral("ErrorIrresolvableConflict");
                else items.remove(id);
            } else if (!master.isEmpty()) {
                items[master].deleted.append(items[master].modified.takeAt(index).originalStart);
                bump(master);
            } else {
                code = QStringLiteral("ErrorItemNotFound");
            }
        }
        message(w, QStringLiteral("DeleteItemResponseMessage"), code);
    }
}

void EwsFakeServer::folders(QXmlStreamWriter &w, const QString &operation)
{
    auto folder = [&](const QString &id, const QString &name, bool writable) {
        w.writeStartElement(T, QStringLiteral("CalendarFolder"));
        w.writeStartElement(T, QStringLiteral("FolderId"));
        w.writeAttribute(QStringLiteral("Id"), id);
        w.writeEndElement();
        text(w, QStringLiteral("DisplayName"), name);
        text(w, QStringLiteral("FolderClass"), QStringLiteral("IPF.Appointment"));
        w.writeStartElement(T, QStringLiteral("EffectiveRights"));
        text(w, QStringLiteral("CreateContents"), writable ? QStringLiteral("true") : QStringLiteral("false"));
        w.writeEndElement();
        w.writeEndElement();
    };
    message(w, operation + QStringLiteral("ResponseMessage"), QStringLiteral("NoError"), [&] {
        if (operation == QLatin1String("GetFolder")) {
            w.writeStartElement(M, QStringLiteral("Folders"));
            folder(QStringLiteral("cal-default"), QStringLiteral("Calendar"), true);
        } else {
            w.writeStartElement(M, QStringLiteral("RootFolder"));
            w.writeStartElement(T, QStringLiteral("Folders"));
            folder(QStringLiteral("cal-team"), QStringLiteral("Team <shared> & co"), false);
            w.writeEndElement();
        }
        w.writeEndElement();
    });
}
