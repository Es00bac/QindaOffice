// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook items → QindaMail's records (M76, D-085): what the .pst import
// (pst_import.h) and the .msg open (mail_service_files.cpp) store for an
// appointment, a task, a contact group's members, and the identities that
// make an import idempotent.
//
// AGENT-CONTRACT (docs/mail-interchange.md "Outlook data files"):
// - itemKey(store, nid) = "pst:<record key hex>:<node id hex>" names one item
//   of one file; uidFor(key) is the version-5 UUID of it (a contact's vCard
//   UID, so a group's member "urn:uuid:<uid>" finds the contact imported
//   from the same file). The same file imported twice gives the same keys,
//   so records are replaced, never duplicated.
// - Events are records of the local calendar (calendarId "local") with wall
//   times in the event's own zone (the Windows zone name mapped to IANA as
//   mail_exchange does; the system zone when the item names none) and the
//   recurrence through mail_exchange's patternToRule (Exchange keeps the
//   same pattern), deleted occurrences as `exceptions`.

#include "outlook_items.h"

#include <QVariantMap>

namespace QindaOffice::Mail {
class ContactStore;
}

namespace QindaOffice::Apps::Mail::OutlookRecords {

[[nodiscard]] QString itemKey(const QByteArray &storeKey, quint32 nid);
[[nodiscard]] QString uidFor(const QString &key);
// False (no record) for an appointment without a start.
bool eventRecord(const QindaOffice::Mail::OutlookAppointment &appointment, const QString &id, QVariantMap *out);
[[nodiscard]] QVariantMap taskRecord(const QindaOffice::Mail::OutlookTask &task, const QString &id, int order);
// todo | doing | done for a task's Outlook status.
[[nodiscard]] QString taskStatus(const QindaOffice::Mail::OutlookTask &task);
// A group's members: a contact of the same file by its uid, anyone else by
// address (linked to the address-book contact with that address if any).
[[nodiscard]] QList<QindaOffice::Mail::ContactMember> groupMembers(const QList<QindaOffice::Mail::DistListMember> &members,
                                                                   const QByteArray &storeKey,
                                                                   const QindaOffice::Mail::ContactStore *contacts);

} // namespace QindaOffice::Apps::Mail::OutlookRecords
