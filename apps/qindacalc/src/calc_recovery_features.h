// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// JSON for the M66 (D-075) state the crash-recovery payload carries:
// hyperlinks, outline and hidden lines, sheet and workbook protection, and
// a sheet's tie to its source package (part, code name, structural
// journal, modelled drawing objects). calc_recovery.h owns the envelope;
// absent keys in older payloads read as "none".

#include "workbook.h"

#include <QJsonArray>
#include <QJsonObject>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonArray hyperlinksToJson(const QList<CalcEng::Hyperlink> &links);
[[nodiscard]] QList<CalcEng::Hyperlink> hyperlinksFromJson(const QJsonArray &array);

[[nodiscard]] QJsonObject outlineToJson(const CalcEng::SheetOutline &outline);
[[nodiscard]] CalcEng::SheetOutline outlineFromJson(const QJsonObject &object);

[[nodiscard]] QJsonObject protectionToJson(const CalcEng::SheetProtection &protection);
[[nodiscard]] CalcEng::SheetProtection protectionFromJson(const QJsonObject &object);

[[nodiscard]] QJsonObject workbookProtectionToJson(const CalcEng::WorkbookProtection &protection);
[[nodiscard]] CalcEng::WorkbookProtection workbookProtectionFromJson(const QJsonObject &object);

[[nodiscard]] QJsonObject sheetSourceToJson(const CalcEng::SheetSource &source);
[[nodiscard]] CalcEng::SheetSource sheetSourceFromJson(const QJsonObject &object);

} // namespace QindaOffice::Apps::QindaCalc
