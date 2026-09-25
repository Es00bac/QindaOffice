// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// EWS CalendarItem <-> store event record (docs/mail.md "Exchange and
// Microsoft 365 calendars" has the field table).
//
// AGENT-NOTE: times travel as UTC xs:dateTime ("...Z") and the zone as a
// Windows name in StartTimeZone/EndTimeZone (Exchange 2010+, read back as
// StartTimeZoneId); the record keeps the wall time in the IANA zone. An
// all-day item starts at local midnight of that zone, so its dates come
// from the wall time, never from the UTC date. Recurring masters carry
// DeletedOccurrences (-> exceptions) and ModifiedOccurrences (-> overrides,
// with subject/location/body read from the exception items themselves).

#include "calendar_sync.h"
#include "exchange_common.h"
#include "xml_tree.h"

#include <QHash>
#include <QXmlStreamWriter>

namespace QindaOffice::Mail::Ews {

// AdditionalProperties for a full master/single item and for exception items.
const QStringList &itemFieldUris();
const QStringList &occurrenceFieldUris();

struct MapContext {
    QString defaultZone;
    QStringList preferredZones;     // Zones::toIana preference for this item
};

// `exceptionItems`: the ModifiedOccurrences' own items by ItemId.
CalendarSync::RemoteEvent mapItem(const XmlNode &item, const QHash<QString, const XmlNode *> &exceptionItems,
                                  const MapContext &context);

struct WriteOptions {
    bool firstDayOfWeek = true;     // WeeklyRecurrence/FirstDayOfWeek needs Exchange 2010 SP1+
};

// <t:CalendarItem> for CreateItem.
bool writeNewItem(QXmlStreamWriter &w, const QVariantMap &record, const WriteOptions &options, QString *error);
// SetItemField/DeleteItemField entries for the series-level changes.
// `recurringOnServer`: the server item is a series master today.
bool writeSeriesUpdates(QXmlStreamWriter &w, const QVariantMap &record, const SeriesChanges &changes,
                        bool recurringOnServer, const WriteOptions &options, QString *error);
// SetItemField entries giving one occurrence (original start `key`) the
// override's times and texts, or the series' values when `entry` is empty.
bool writeOccurrenceUpdates(QXmlStreamWriter &w, const QVariantMap &master, const QString &key,
                            const QVariantMap &entry, QString *error);

} // namespace QindaOffice::Mail::Ews
