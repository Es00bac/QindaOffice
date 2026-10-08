// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to mspdi_*.cpp: the MSPDI wire conventions (mspdi.h lists them)
// as exact converters. Nothing here uses floating point for money or time.

#include "plan_types.h"

#include <QStringList>

#include <optional>

namespace QindaOffice::Plan::Mspdi {

inline constexpr int kUnassignedResource = -65535; // Project's placeholder resource UID
inline const QString kNamespace = QStringLiteral("http://schemas.microsoft.com/project");

// "PT40H0M0S", "PT8H30M0S", "-PT8H0M0S", "P1DT2H" (a D is 24 hours, as in
// xs:duration). Seconds round to the nearest minute.
[[nodiscard]] std::optional<Minutes> parseWireDuration(const QString &text);
[[nodiscard]] QString wireDuration(Minutes minutes);

// "yyyy-MM-ddTHH:mm:ss" (seconds ignored); kNoDate when empty or invalid.
[[nodiscard]] Minutes parseWireDateTime(const QString &text);
[[nodiscard]] QString wireDateTime(Minutes instant);

// "08:00:00" → 480. An END time of "00:00:00" or "24:00:00" is midnight at
// the end of the day (1440), as Project writes a shift that runs to 24:00.
[[nodiscard]] std::optional<int> parseTimeOfDay(const QString &text, bool end);
[[nodiscard]] QString formatTimeOfDay(int minute);

// A decimal string times 10^scale, rounded half away from zero; nullopt
// when it is not a number. "1.005" at scale 2 → 101.
[[nodiscard]] std::optional<qint64> scaledDecimal(const QString &text, int scale);
// value / 10^scale with exactly `scale` decimals ("1.50").
[[nodiscard]] QString decimalText(qint64 value, int scale);

// MSPDI cost (cents) ↔ minor units of a currency with `decimals` digits.
[[nodiscard]] qint64 centsToMinor(qint64 cents, int decimals);
[[nodiscard]] qint64 minorToCents(qint64 minor, int decimals);

// DurationFormat / LagFormat codes: 3 m, 5 h, 7 d, 9 w, 11 mo; the even
// codes 4–12 are the elapsed variants; 19 %, 20 e%, 21 null; +32 is the
// estimated variant of each (35–53).
struct DurationCode {
    DurationUnit unit = DurationUnit::Days;
    bool estimated = false;
    bool elapsed = false;
    bool percent = false;
    bool known = true;
};
[[nodiscard]] DurationCode durationCode(int code);
[[nodiscard]] int durationFormat(DurationUnit unit, bool estimated);

// PredecessorLink Type: 0 FF, 1 FS, 2 SF, 3 SS.
[[nodiscard]] std::optional<LinkType> linkTypeFromWire(int code);
[[nodiscard]] int wireLinkType(LinkType type);

// ---- the reader's raw records (mspdi_reader.cpp fills, mspdi_assemble.cpp
// validates and converts; money is still in MSPDI cents here) ----
struct RawLink {
    int predecessorUid = 0;
    int type = 1;
    qint64 lagTenths = 0;
    int lagFormat = 7;
    bool crossProject = false;
};

struct RawBaseline {
    int number = 0;
    BaselineData data;
    qint64 costCents = 0;
};

struct RawTask {
    Task task;
    int id = -1;
    bool isNull = false;
    int outlineLevel = 1;
    int durationFormat = 7;
    int constraintCode = 0;
    int calendarUid = -1;
    Minutes start = kNoDate;
    Minutes finish = kNoDate;
    Minutes manualStart = kNoDate;
    Minutes manualFinish = kNoDate;
    qint64 fixedCostCents = 0;
    QList<RawLink> links;
    QList<RawBaseline> baselines;
};

struct RawResource {
    Resource resource;
    bool isNull = false;
    int type = 1;
    qint64 rateCents = 0;
    qint64 costPerUseCents = 0;
};

struct RawAssignment {
    int uid = 0;
    int taskUid = 0;
    int resourceUid = 0;
    qint64 units = 100;
    QJsonObject extra; // kept elements and their stamp (mspdi_keep.h)
};

struct Raw {
    ProjectInfo info;
    QString name;
    bool hasCalendarUid = false;
    QList<Calendar> calendars;
    QList<RawTask> tasks;
    QList<RawResource> resources;
    QList<RawAssignment> assignments;
    QStringList warnings;
    void warn(const QString &message)
    {
        if (!warnings.contains(message)) {
            warnings.append(message);
        }
    }
};

// Validates, converts and schedules the raw records into *out.
void assemble(Raw &raw, Project *out);

} // namespace QindaOffice::Plan::Mspdi
