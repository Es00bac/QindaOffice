// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Windows time-zone names (what Exchange and Microsoft Graph store) <-> IANA
// ids (what LocalProductivityStore records carry).
//
// AGENT-NOTE: the table is Qt's own copy of CLDR's windowsZones.xml
// (QTimeZone::windowsIdToDefaultIanaId / ianaIdToWindowsId; Qt 6.11 generates
// it from CLDR with qtbase's util/locale_database/cldr2qtimezone.py). The
// toolkit's CalDAV codec resolves Windows TZIDs through the same call, so a
// zone maps identically whichever provider brought the event in. Nothing
// here ships a second table that could drift from Qt's.
// AGENT-GUARD: a Windows zone covers several IANA zones (W. Europe Standard
// Time is Berlin, Zurich, Rome, ...). `preferred` keeps the zone the record
// already had, so a write and the re-read after it never swap Europe/Zurich
// for CLDR's default Europe/Berlin (that would look like an edit).

#include <QString>
#include <QStringList>

namespace QindaOffice::Mail::Zones {

// IANA id for a Windows name, an IANA id (passed through), or Graph's
// "tzone://Microsoft/Utc"; empty when unknown (custom zones). The first
// entry of `preferred` that maps to the same Windows zone wins.
QString toIana(const QString &name, const QStringList &preferred = {});
// Windows name for an IANA id ("UTC" for UTC); empty when CLDR has none.
QString toWindows(const QString &iana);

} // namespace QindaOffice::Mail::Zones
