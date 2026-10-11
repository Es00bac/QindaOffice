// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "workbook.h"
#include <QSizeF>
#include <QMarginsF>
namespace QindaOffice::Calc::IO {
struct HeaderPicture { QByteArray bytes; QSizeF points; QMarginsF crop; };
struct PrintedNote { CellAddr cell; QSizeF points; std::optional<CellAddr> anchor; qreal dx = 0; qreal dy = 0; };
struct LegacyPrintAssets {
    QHash<QString, HeaderPicture> pictures; // VML shape ids: LH/CH/RH/LF/CF/RF, with F/E variants
    QList<PrintedNote> notes;               // visible legacy notes only
};
// Read once per print job from the retained OPC package. No external
// relationships are fetched; absent/invalid parts simply supply no asset.
[[nodiscard]] LegacyPrintAssets legacyPrintAssets(const Workbook &book, qint32 sheet);
}
