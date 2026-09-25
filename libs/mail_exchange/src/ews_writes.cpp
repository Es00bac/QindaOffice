// SPDX-License-Identifier: GPL-3.0-or-later
// EwsBackend writes: UpdateItem (series and occurrences), DeleteItem, and the
// calendar folder listing. Reads and CreateItem are in ews_backend.cpp.
#include "ews_backend.h"

#include "ews_backend_p.h"
#include "ews_mapping.h"

#include <qindatk/recurrence_service.h>

#include <climits>

namespace QindaOffice::Mail {
using CalendarSync::LocalItem;
using CalendarSync::RemoteEvent;
using CalendarSync::WriteOutcome;
using Failure = QindaTK::Protocol::Failure;
using namespace EwsDetail;

namespace {
struct OccurrenceRef {
    QString itemId;                 // an exception's own ItemId, or
    int index = -1;                 // the InstanceIndex of a plain occurrence
};

// 1-based position of occurrence `key` in the series pattern; -1 if unknown.
int instanceIndex(const QVariantMap &master, const QString &key)
{
    const QVariantMap rule = master.value(QStringLiteral("recurrenceRule")).toMap();
    EventTimes times;
    QString error;
    if (rule.isEmpty() || !eventTimes(master, &times, &error)) return -1;
    const QDateTime target = wallTime(key, times.zone);
    if (!target.isValid()) return -1;
    const QString start = canonicalKey(master.value(QStringLiteral("startLocal")).toString());
    const qint64 span = QindaTK::RecurrenceService::spanSeconds(
        start, canonicalKey(master.value(QStringLiteral("endLocal")).toString()), times.zone);
    QindaTK::RecurrenceService recurrence;
    const QVariantList rows = recurrence.expand(QStringLiteral("x"), start, int(qBound(qint64(0), span, qint64(INT_MAX))), times.zone,
        rule, times.start.toUTC().toString(Qt::ISODate), target.addSecs(1).toUTC().toString(Qt::ISODate), 1000);
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (rows.at(i).toMap().value(QStringLiteral("localStart")).toString() == key) return int(i + 1);
    return -1;
}

void writeRef(QXmlStreamWriter &w, const QString &masterId, const OccurrenceRef &ref)
{
    if (!ref.itemId.isEmpty()) {
        writeItemId(w, ref.itemId);
        return;
    }
    Ews::startT(w, QStringLiteral("OccurrenceItemId"));
    w.writeAttribute(QStringLiteral("RecurringMasterId"), masterId);
    w.writeAttribute(QStringLiteral("InstanceIndex"), QString::number(ref.index));
    w.writeEndElement();
}

void startUpdate(QXmlStreamWriter &w)
{
    Ews::startM(w, QStringLiteral("UpdateItem"));
    w.writeAttribute(QStringLiteral("MessageDisposition"), QStringLiteral("SaveOnly"));
    w.writeAttribute(QStringLiteral("ConflictResolution"), QStringLiteral("NeverOverwrite"));
    w.writeAttribute(QStringLiteral("SendMeetingInvitationsOrCancellations"), QStringLiteral("SendToNone"));
    Ews::startM(w, QStringLiteral("ItemChanges"));
}
} // namespace

WriteOutcome EwsBackend::update(const LocalItem &item, const QString &version)
{
    const QVariantMap baseline = item.baseline();
    const SeriesChanges changes = seriesChanges(baseline, item.record);
    const bool recurringOnServer = item.record.value(QStringLiteral("remoteItemType")).toString() == QLatin1String("seriesMaster");
    QString current = version;
    QString error;
    QByteArray scratch;
    QXmlStreamWriter probe(&scratch);
    if (!Ews::writeSeriesUpdates(probe, item.record, changes, recurringOnServer, writeOptions(), &error))
        return outcome(WriteOutcome::Conflict, error);          // the server cannot hold this edit
    if (!scratch.isEmpty()) {
        Ews::Response response;
        if (!call(QStringLiteral("UpdateItem"), [&](QXmlStreamWriter &w) {
                startUpdate(w);
                Ews::startT(w, QStringLiteral("ItemChange"));
                writeItemId(w, item.remoteId, version);
                Ews::startT(w, QStringLiteral("Updates"));
                Ews::writeSeriesUpdates(w, item.record, changes, recurringOnServer, writeOptions(), &error);
                w.writeEndElement();
                w.writeEndElement();
                w.writeEndElement();
                w.writeEndElement();
            }, &response))
            return outcome(WriteOutcome::Failed, transport().error());
        if (response.messages.size() != 1) return outcome(WriteOutcome::Failed, QStringLiteral("Unexpected UpdateItem answer"));
        const WriteOutcome updated = outcomeOf(response.messages.first(), item.remoteId);
        if (updated.kind != WriteOutcome::Done) return updated;
        current = updated.version;
    }
    if (!occurrencesNeedWrite(baseline, item.record, changes)) return outcome(WriteOutcome::Done, {}, item.remoteId, current);
    // Nothing was written yet when only occurrences changed: the ChangeKey
    // check inside applyOccurrences is then the conditional part.
    return applyOccurrences(item, item.remoteId, current, scratch.isEmpty());
}

WriteOutcome EwsBackend::applyOccurrences(const LocalItem &item, const QString &remoteId, const QString &version,
                                          bool conditional)
{
    QMap<QString, RemoteEvent> current;
    if (!fetch({remoteId}, &current)) return outcome(WriteOutcome::Failed, transport().error());
    if (!current.contains(remoteId)) return outcome(WriteOutcome::Gone);
    const RemoteEvent server = current.value(remoteId);
    if (conditional && !version.isEmpty() && server.version != version) return outcome(WriteOutcome::Conflict);
    const OccurrenceChanges changes = occurrenceChanges(server.fields, item.record);
    if (!changes.reinstated.isEmpty())
        return outcome(WriteOutcome::Conflict, QStringLiteral("Exchange cannot bring back a deleted occurrence (%1)")
                                                   .arg(changes.reinstated.join(QStringLiteral(", "))));
    const QVariantMap known = server.fields.value(QStringLiteral("remoteOccurrences")).toMap();
    QMap<QString, OccurrenceRef> refs;
    QVariantList updates = changes.overrides + changes.restored;
    QStringList keys = changes.excluded;
    for (const QVariant &entry : std::as_const(updates)) keys.append(canonicalKey(entry.toMap().value(QStringLiteral("recurrenceId")).toString()));
    for (const QString &key : std::as_const(keys)) {
        OccurrenceRef ref{known.value(key).toString(), -1};
        if (ref.itemId.isEmpty()) ref.index = instanceIndex(server.fields, key);
        if (ref.itemId.isEmpty() && ref.index < 1)
            return outcome(WriteOutcome::Conflict, QStringLiteral("The occurrence %1 cannot be addressed on Exchange").arg(key));
        refs.insert(key, ref);
    }
    QString error;
    for (const QVariant &value : std::as_const(updates)) {
        QByteArray scratch;
        QXmlStreamWriter probe(&scratch);
        const QVariantMap entry = value.toMap();
        if (!Ews::writeOccurrenceUpdates(probe, item.record, canonicalKey(entry.value(QStringLiteral("recurrenceId")).toString()),
                                         entry.size() > 1 ? entry : QVariantMap(), &error))
            return outcome(WriteOutcome::Conflict, error);
    }
    auto check = [&](const Ews::Response &response) {
        for (const Ews::ResponseMessage &message : response.messages)
            if (!message.success() && message.code != QLatin1String("ErrorItemNotFound")) return outcomeOf(message, remoteId);
        return outcome(WriteOutcome::Done, {}, remoteId);
    };
    if (!changes.excluded.isEmpty()) {
        Ews::Response response;
        if (!call(QStringLiteral("DeleteItem"), [&](QXmlStreamWriter &w) {
                Ews::startM(w, QStringLiteral("DeleteItem"));
                w.writeAttribute(QStringLiteral("DeleteType"), QStringLiteral("MoveToDeletedItems"));
                w.writeAttribute(QStringLiteral("SendMeetingCancellations"), QStringLiteral("SendToNone"));
                Ews::startM(w, QStringLiteral("ItemIds"));
                for (const QString &key : changes.excluded) writeRef(w, remoteId, refs.value(key));
                w.writeEndElement();
                w.writeEndElement();
            }, &response))
            return outcome(WriteOutcome::Failed, transport().error());
        const WriteOutcome deleted = check(response);
        if (deleted.kind != WriteOutcome::Done) return deleted;
    }
    if (!updates.isEmpty()) {
        Ews::Response response;
        if (!call(QStringLiteral("UpdateItem"), [&](QXmlStreamWriter &w) {
                startUpdate(w);
                for (const QVariant &value : std::as_const(updates)) {
                    const QVariantMap entry = value.toMap();
                    const QString key = canonicalKey(entry.value(QStringLiteral("recurrenceId")).toString());
                    Ews::startT(w, QStringLiteral("ItemChange"));
                    writeRef(w, remoteId, refs.value(key));
                    Ews::startT(w, QStringLiteral("Updates"));
                    Ews::writeOccurrenceUpdates(w, item.record, key, entry.size() > 1 ? entry : QVariantMap(), &error);
                    w.writeEndElement();
                    w.writeEndElement();
                }
                w.writeEndElement();
                w.writeEndElement();
            }, &response))
            return outcome(WriteOutcome::Failed, transport().error());
        const WriteOutcome updated = check(response);
        if (updated.kind != WriteOutcome::Done) return updated;
    }
    return outcome(WriteOutcome::Done, {}, remoteId);    // the engine re-reads the new ChangeKey
}

WriteOutcome EwsBackend::remove(const LocalItem &item, const QString &version)
{
    Ews::Response response;
    if (!call(QStringLiteral("DeleteItem"), [&](QXmlStreamWriter &w) {
            Ews::startM(w, QStringLiteral("DeleteItem"));
            w.writeAttribute(QStringLiteral("DeleteType"), QStringLiteral("MoveToDeletedItems"));
            w.writeAttribute(QStringLiteral("SendMeetingCancellations"), QStringLiteral("SendToNone"));
            Ews::startM(w, QStringLiteral("ItemIds"));
            writeItemId(w, item.remoteId, version);
            w.writeEndElement();
            w.writeEndElement();
        }, &response))
        return outcome(WriteOutcome::Failed, transport().error());
    if (response.messages.size() != 1) return outcome(WriteOutcome::Failed, QStringLiteral("Unexpected DeleteItem answer"));
    return outcomeOf(response.messages.first(), item.remoteId);
}

bool EwsBackend::listCalendars(QVariantList *out)
{
    auto folder = [](const XmlNode &node, bool isDefault) {
        const XmlNode *id = node.child(u"FolderId");
        const XmlNode *rights = node.child(u"EffectiveRights");
        return QVariantMap{{QStringLiteral("id"), id ? id->attribute(QStringLiteral("Id")) : QString()},
            {QStringLiteral("name"), node.childText(u"DisplayName")}, {QStringLiteral("color"), QString()},
            {QStringLiteral("readOnly"), rights && rights->childText(u"CreateContents") == QLatin1String("false")},
            {QStringLiteral("isDefault"), isDefault}};
    };
    auto shape = [](QXmlStreamWriter &w, const QStringList &uris) {
        Ews::startM(w, QStringLiteral("FolderShape"));
        Ews::textT(w, QStringLiteral("BaseShape"), QStringLiteral("Default"));
        Ews::startT(w, QStringLiteral("AdditionalProperties"));
        for (const QString &uri : uris) Ews::fieldUri(w, uri);
        w.writeEndElement();
        w.writeEndElement();
    };
    auto calendarRoot = [](QXmlStreamWriter &w) {
        Ews::startT(w, QStringLiteral("DistinguishedFolderId"));
        w.writeAttribute(QStringLiteral("Id"), QStringLiteral("calendar"));
        w.writeEndElement();
    };
    Ews::Response root;
    if (!call(QStringLiteral("GetFolder"), [&](QXmlStreamWriter &w) {
            Ews::startM(w, QStringLiteral("GetFolder"));
            shape(w, {QStringLiteral("folder:EffectiveRights")});
            Ews::startM(w, QStringLiteral("FolderIds"));
            calendarRoot(w);
            w.writeEndElement();
            w.writeEndElement();
        }, &root))
        return false;
    const XmlNode *folders = root.messages.isEmpty() || !root.messages.first().success() ? nullptr
                                                                                       : root.messages.first().node->child(u"Folders");
    if (!folders || folders->children.isEmpty()) {
        transport().fail(Failure::Rejected, QStringLiteral("Exchange did not return the calendar folder"));
        return false;
    }
    out->append(folder(folders->children.first(), true));
    Ews::Response children;
    if (!call(QStringLiteral("FindFolder"), [&](QXmlStreamWriter &w) {
            Ews::startM(w, QStringLiteral("FindFolder"));
            w.writeAttribute(QStringLiteral("Traversal"), QStringLiteral("Deep"));
            shape(w, {QStringLiteral("folder:FolderClass"), QStringLiteral("folder:EffectiveRights")});
            Ews::startM(w, QStringLiteral("ParentFolderIds"));
            calendarRoot(w);
            w.writeEndElement();
            w.writeEndElement();
        }, &children))
        return false;
    const XmlNode *found = children.messages.isEmpty() ? nullptr : children.messages.first().node->find(u"Folders");
    for (const XmlNode &node : found ? found->children : QList<XmlNode>())
        if (node.name == u"CalendarFolder" || node.childText(u"FolderClass").startsWith(QLatin1String("IPF.Appointment")))
            out->append(folder(node, false));
    return true;
}

} // namespace QindaOffice::Mail
