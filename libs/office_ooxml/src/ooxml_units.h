// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Office Open XML measurement units and the suite's document units.
//
//   twip         1/1440 inch   (WordprocessingML lengths: w:ind, w:pgSz, …)
//   half-point   1/144 inch    (w:sz font sizes)
//   eighth-point 1/576 inch    (w:sz on borders)
//   EMU          1/914400 inch (DrawingML: 12700 per point, 9525 per px)
//   px           1/96 inch     (QindaWrite document units, QTextDocument)
//   pt           1/72 inch     (QindaShow slide units)
//
// Universal measures ("2.5in", "12pt", "1cm") appear in Strict documents
// and some Transitional attributes; parseUniversalMeasure handles them.

#include <QString>
#include <QtGlobal>

#include <cmath>
#include <optional>

namespace QindaOffice::Ooxml::Units {

inline constexpr double kTwipsPerInch = 1440.0;
inline constexpr double kEmuPerInch = 914400.0;
inline constexpr double kEmuPerPoint = 12700.0;
inline constexpr double kEmuPerPx = 9525.0;
inline constexpr double kPxPerInch = 96.0;

[[nodiscard]] constexpr double twipsToPx(double twips) { return twips / 15.0; }
[[nodiscard]] constexpr double pxToTwips(double px) { return px * 15.0; }
[[nodiscard]] constexpr double twipsToPt(double twips) { return twips / 20.0; }
[[nodiscard]] constexpr double ptToTwips(double pt) { return pt * 20.0; }
[[nodiscard]] constexpr double emuToPx(double emu) { return emu / kEmuPerPx; }
[[nodiscard]] constexpr double pxToEmu(double px) { return px * kEmuPerPx; }
[[nodiscard]] constexpr double emuToPt(double emu) { return emu / kEmuPerPoint; }
[[nodiscard]] constexpr double ptToEmu(double pt) { return pt * kEmuPerPoint; }
[[nodiscard]] constexpr double ptToPx(double pt) { return pt * 96.0 / 72.0; }
[[nodiscard]] constexpr double pxToPt(double px) { return px * 72.0 / 96.0; }

[[nodiscard]] inline qint64 roundToInt64(double value) { return qint64(std::llround(value)); }

// "1.5in", "12pt", "2cm", "10mm", "3pc", "5pi" → inches; a bare number is
// returned as std::nullopt (the caller knows its native unit).
[[nodiscard]] inline std::optional<double> parseUniversalMeasureInches(const QString &value)
{
    static constexpr struct {
        const char *suffix;
        double perInch;
    } kUnits[] = {{"in", 1.0}, {"pt", 72.0}, {"cm", 2.54}, {"mm", 25.4}, {"pc", 6.0}, {"pi", 6.0}};
    const QString trimmed = value.trimmed();
    for (const auto &unit : kUnits) {
        if (trimmed.endsWith(QLatin1String(unit.suffix))) {
            bool ok = false;
            const double number = trimmed.chopped(2).toDouble(&ok);
            if (ok) {
                return number / unit.perInch;
            }
        }
    }
    return std::nullopt;
}

// A twips attribute that may also be a universal measure; `fallback` when
// absent or unparsable.
[[nodiscard]] inline double parseTwips(const QString &value, double fallback = 0.0)
{
    if (value.isEmpty()) {
        return fallback;
    }
    if (const auto inches = parseUniversalMeasureInches(value)) {
        return *inches * kTwipsPerInch;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? number : fallback;
}

// An EMU coordinate that may also be a universal measure.
[[nodiscard]] inline double parseEmu(const QString &value, double fallback = 0.0)
{
    if (value.isEmpty()) {
        return fallback;
    }
    if (const auto inches = parseUniversalMeasureInches(value)) {
        return *inches * kEmuPerInch;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? number : fallback;
}

// OOXML booleans: "1"/"true"/"on" (and absent value = true for toggles).
[[nodiscard]] inline bool parseOnOff(const QString &value, bool absentMeans = true)
{
    if (value.isEmpty()) {
        return absentMeans;
    }
    return value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || value.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0;
}

} // namespace QindaOffice::Ooxml::Units
