// SPDX-License-Identifier: GPL-3.0-or-later
#include "ews_backend.h"

#include "ews_backend_p.h"
#include "ews_mapping.h"

#include <QSet>
#include <cmath>

namespace QindaOffice::Mail {
using CalendarSync::LocalItem;
using CalendarSync::RemoteEvent;
using CalendarSync::WriteOutcome;
using Failure = QindaTK::Protocol::Failure;

using namespace EwsDetail;

EwsBackend::EwsBackend(const EwsConfig &config)
    : Backend(config.timeoutMs, config.allowInsecureAuth), m_config(config)
{
    m_header.serverVersion = config.serverVersion.isEmpty() ? QStringLiteral("Exchange2013_SP1") : config.serverVersion;
    m_header.impersonate = config.impersonate;
    if (config.secret.isEmpty()) return;
    if (config.mechanism == QLatin1String("bearer")) {
        transport().setAuthorization("Bearer " + config.secret.toUtf8(), {config.secret});
    } else {
        const QByteArray basic = (config.userName + QLatin1Char(':') + config.secret).toUtf8().toBase64();
        transport().setAuthorization("Basic " + basic, {config.secret, QString::fromLatin1(basic)});
    }
}

bool EwsBackend::call(const QString &operation, const Ews::BodyWriter &body, Ews::Response *response)
{
    HttpTransport::Headers headers{{"Content-Type", "text/xml; charset=utf-8"}, {"Accept", "text/xml"},
                                   {"SOAPAction", Ews::soapAction(operation)}};
    const QString anchor = m_config.impersonate.isEmpty() ? m_config.userName : m_config.impersonate;
    if (!anchor.isEmpty()) headers.append(qMakePair(QByteArray("X-AnchorMailbox"), anchor.toUtf8()));   // Exchange Online routing
    HttpTransport::Response http;
    if (!transport().send("POST", m_config.url, headers, Ews::envelope(m_header, body), operation, &http)) return false;
    if (http.status != 200 && http.status != 500) {
        transport().fail(Failure::Rejected, QStringLiteral("EWS %1 returned HTTP %2").arg(operation).arg(http.status));
        return false;
    }
    QString error;
    if (!Ews::parse(http.body, response, &error)) {
        transport().fail(Failure::Rejected, QStringLiteral("EWS %1: %2").arg(operation, error));
        return false;
    }
    int backOff = response->backOffMs;
    bool busy = response->faultCode == QLatin1String("ErrorServerBusy");
    for (const Ews::ResponseMessage &message : std::as_const(response->messages)) {
        if (message.code != QLatin1String("ErrorServerBusy")) continue;
        busy = true;
        backOff = qMax(backOff, message.backOffMs);
    }
    if (busy) {
        const int seconds = backOff >= 0 ? int(std::ceil(backOff / 1000.0)) : -1;
        transport().fail(Failure::Offline, QStringLiteral("Exchange is throttling requests; retry later"), seconds);
        return false;
    }
    if (!response->fault.isEmpty()) {
        transport().fail(Failure::Rejected, QStringLiteral("Exchange refused %1: %2").arg(operation, response->fault));
        return false;
    }
    return true;
}

void EwsBackend::writeFolder(QXmlStreamWriter &w) const
{
    if (m_config.folderId.isEmpty()) {
        Ews::startT(w, QStringLiteral("DistinguishedFolderId"));
        w.writeAttribute(QStringLiteral("Id"), QStringLiteral("calendar"));
    } else {
        Ews::startT(w, QStringLiteral("FolderId"));
        w.writeAttribute(QStringLiteral("Id"), m_config.folderId);
    }
    w.writeEndElement();
}

bool EwsBackend::list(QMap<QString, RemoteEvent> *out)
{
    Ews::Response &view = m_responses.emplace_back();
    const bool called = call(QStringLiteral("FindItem"), [&](QXmlStreamWriter &w) {
        Ews::startM(w, QStringLiteral("FindItem"));
        w.writeAttribute(QStringLiteral("Traversal"), QStringLiteral("Shallow"));
        Ews::itemShape(w, QStringLiteral("IdOnly"), {QStringLiteral("calendar:CalendarItemType"), QStringLiteral("calendar:UID")});
        Ews::startM(w, QStringLiteral("CalendarView"));
        w.writeAttribute(QStringLiteral("MaxEntriesReturned"), QString::number(m_config.maxViewEntries));
        w.writeAttribute(QStringLiteral("StartDate"), utc(m_config.window.startUtc));
        w.writeAttribute(QStringLiteral("EndDate"), utc(m_config.window.endUtc));
        w.writeEndElement();
        Ews::startM(w, QStringLiteral("ParentFolderIds"));
        writeFolder(w);
        w.writeEndElement();
        w.writeEndElement();
    }, &view);
    if (!called) return false;
    if (view.messages.size() != 1 || !view.messages.first().success()) {
        const Ews::ResponseMessage message = view.messages.value(0);
        transport().fail(Failure::Rejected, QStringLiteral("Exchange could not list the calendar: %1 %2").arg(message.code, message.text));
        return false;
    }
    const XmlNode *root = view.messages.first().node->child(u"RootFolder");
    if (root && root->attribute(QStringLiteral("IncludesLastItemInRange")) == QLatin1String("false")) {
        transport().fail(Failure::Rejected, QStringLiteral("The sync window holds more than %1 events; shorten syncPastDays "
                                                           "or syncFutureDays").arg(m_config.maxViewEntries));
        return false;
    }
    // Singles are listed directly; occurrences and exceptions lead to their
    // master (one lookup per series, grouped by UID).
    QStringList occurrenceIds;
    QSet<QString> series;
    const XmlNode *items = root ? root->child(u"Items") : nullptr;
    for (const XmlNode &item : items ? items->children : QList<XmlNode>()) {
        const XmlNode *id = item.child(u"ItemId");
        if (!id) continue;
        const QString type = item.childText(u"CalendarItemType");
        const QString itemId = id->attribute(QStringLiteral("Id"));
        if (type == QLatin1String("Single") || type == QLatin1String("RecurringMaster")) {
            out->insert(itemId, {itemId, id->attribute(QStringLiteral("ChangeKey")), false, {}});
        } else {
            const QString group = item.childText(u"UID").isEmpty() ? itemId : item.childText(u"UID");
            if (!series.contains(group)) { series.insert(group); occurrenceIds.append(itemId); }
        }
    }
    for (qsizetype from = 0; from < occurrenceIds.size(); from += kBatch) {
        const QStringList batch = occurrenceIds.mid(from, kBatch);
        Ews::Response &masters = m_responses.emplace_back();
        if (!call(QStringLiteral("GetItem"), [&](QXmlStreamWriter &w) {
                Ews::startM(w, QStringLiteral("GetItem"));
                Ews::itemShape(w, QStringLiteral("IdOnly"), {});
                Ews::startM(w, QStringLiteral("ItemIds"));
                for (const QString &id : batch) {
                    Ews::startT(w, QStringLiteral("RecurringMasterItemId"));
                    w.writeAttribute(QStringLiteral("OccurrenceId"), id);
                    w.writeEndElement();
                }
                w.writeEndElement();
                w.writeEndElement();
            }, &masters))
            return false;
        for (const Ews::ResponseMessage &message : std::as_const(masters.messages)) {
            const XmlNode *item = message.success() ? firstItem(message) : nullptr;
            const XmlNode *id = item ? item->child(u"ItemId") : nullptr;
            if (id) out->insert(id->attribute(QStringLiteral("Id")),
                                {id->attribute(QStringLiteral("Id")), id->attribute(QStringLiteral("ChangeKey")), false, {}});
        }
    }
    return true;
}

bool EwsBackend::getItems(const QStringList &ids, const QStringList &fieldUris, QHash<QString, const XmlNode *> *items,
                          bool strict)
{
    for (qsizetype from = 0; from < ids.size(); from += kBatch) {
        const QStringList batch = ids.mid(from, kBatch);
        Ews::Response &response = m_responses.emplace_back();
        if (!call(QStringLiteral("GetItem"), [&](QXmlStreamWriter &w) {
                Ews::startM(w, QStringLiteral("GetItem"));
                Ews::itemShape(w, QStringLiteral("IdOnly"), fieldUris, true);
                Ews::startM(w, QStringLiteral("ItemIds"));
                for (const QString &id : batch) writeItemId(w, id);
                w.writeEndElement();
                w.writeEndElement();
            }, &response))
            return false;
        for (const Ews::ResponseMessage &message : std::as_const(response.messages)) {
            if (const XmlNode *item = message.success() ? firstItem(message) : nullptr) {
                if (const XmlNode *id = item->child(u"ItemId")) items->insert(id->attribute(QStringLiteral("Id")), item);
            } else if (strict && message.code != QLatin1String("ErrorItemNotFound")) {
                // AGENT-GUARD: only ErrorItemNotFound means "deleted"; any other
                // error must not make the caller remove a local record.
                transport().fail(Failure::Rejected, QStringLiteral("Exchange could not read an event: %1 %2").arg(message.code, message.text));
                return false;
            }
        }
    }
    return true;
}

bool EwsBackend::fetch(const QStringList &ids, QMap<QString, RemoteEvent> *out)
{
    QHash<QString, const XmlNode *> items, exceptions;
    if (!getItems(ids, Ews::itemFieldUris(), &items, true)) return false;
    QStringList exceptionIds;
    for (const XmlNode *item : std::as_const(items)) {
        const XmlNode *modified = item->child(u"ModifiedOccurrences");
        for (const XmlNode *occurrence : modified ? modified->childrenNamed(u"Occurrence") : QList<const XmlNode *>())
            if (const XmlNode *id = occurrence->child(u"ItemId")) exceptionIds.append(id->attribute(QStringLiteral("Id")));
    }
    if (!exceptionIds.isEmpty() && !getItems(exceptionIds, Ews::occurrenceFieldUris(), &exceptions, false)) return false;
    for (auto it = items.cbegin(); it != items.cend(); ++it) {
        const Ews::MapContext context{m_config.defaultZone, preferredZones(it.key(), m_config.defaultZone)};
        out->insert(it.key(), Ews::mapItem(*it.value(), exceptions, context));
    }
    return true;
}

WriteOutcome EwsBackend::outcomeOf(const Ews::ResponseMessage &message, const QString &remoteId)
{
    if (message.success()) {
        const XmlNode *item = firstItem(message);
        const XmlNode *id = item ? item->child(u"ItemId") : nullptr;
        return outcome(WriteOutcome::Done, {}, id ? id->attribute(QStringLiteral("Id")) : remoteId,
                       id ? id->attribute(QStringLiteral("ChangeKey")) : QString());
    }
    if (isConflictCode(message.code)) return outcome(WriteOutcome::Conflict);
    if (message.code == QLatin1String("ErrorItemNotFound")) return outcome(WriteOutcome::Gone);
    return outcome(WriteOutcome::Failed, QStringLiteral("%1 %2").arg(message.code, message.text).trimmed());
}

WriteOutcome EwsBackend::create(const LocalItem &item)
{
    QString error;
    const Ews::WriteOptions options = writeOptions();
    // AGENT-NOTE: the item is written into a scratch buffer first, so a record
    // the server cannot express is refused before any request is sent.
    QByteArray scratch;
    QXmlStreamWriter probe(&scratch);
    if (!Ews::writeNewItem(probe, item.record, options, &error)) return outcome(WriteOutcome::Failed, error);
    Ews::Response response;
    const bool called = call(QStringLiteral("CreateItem"), [&](QXmlStreamWriter &w) {
        Ews::startM(w, QStringLiteral("CreateItem"));
        w.writeAttribute(QStringLiteral("SendMeetingInvitations"), QStringLiteral("SendToNone"));
        Ews::startM(w, QStringLiteral("SavedItemFolderId"));
        writeFolder(w);
        w.writeEndElement();
        Ews::startM(w, QStringLiteral("Items"));
        Ews::writeNewItem(w, item.record, options, &error);
        w.writeEndElement();
        w.writeEndElement();
    }, &response);
    if (!called) return outcome(WriteOutcome::Failed, transport().error());
    if (response.messages.size() != 1) return outcome(WriteOutcome::Failed, QStringLiteral("Unexpected CreateItem answer"));
    const WriteOutcome created = outcomeOf(response.messages.first(), {});
    if (created.kind != WriteOutcome::Done || created.remoteId.isEmpty())
        return created.kind == WriteOutcome::Done ? outcome(WriteOutcome::Failed, QStringLiteral("CreateItem returned no ItemId")) : created;
    preferZone(created.remoteId, item.record.value(QStringLiteral("zone")).toString());
    const bool occurrences = !item.record.value(QStringLiteral("exceptions")).toList().isEmpty()
        || !item.record.value(QStringLiteral("overrides")).toList().isEmpty();
    if (!occurrences) return created;
    const WriteOutcome applied = applyOccurrences(item, created.remoteId, created.version, false);
    // The series exists either way: report its identity so it is never created twice.
    if (applied.kind != WriteOutcome::Done) errors().append(QStringLiteral("%1: %2").arg(item.id, applied.error));
    return outcome(WriteOutcome::Done, {}, created.remoteId, applied.kind == WriteOutcome::Done ? applied.version : created.version);
}

Ews::WriteOptions EwsBackend::writeOptions() const
{
    // WeeklyRecurrence/FirstDayOfWeek exists from Exchange2010_SP1 on.
    return {!m_header.serverVersion.startsWith(QLatin1String("Exchange2007"))
            && m_header.serverVersion != QLatin1String("Exchange2010")};
}

} // namespace QindaOffice::Mail
