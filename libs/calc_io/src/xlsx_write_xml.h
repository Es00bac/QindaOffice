// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Part generators for the package pieces QXlsx has no model for: the
// comments part and its legacy VML drawing, the autoFilter element and table
// parts. The callers know the engine model; these produce the bytes.

#include "sheet_rules.h"

#include <QByteArray>

namespace QindaOffice::Calc::IO {

// xl/commentsN.xml for one sheet's comment map (keyed by packed CellAddr).
[[nodiscard]] QByteArray commentsPartXml(const QHash<quint64, CellComment> &comments);

// The <autoFilter> element for a sheet's filter model (empty when inactive).
[[nodiscard]] QByteArray autoFilterXml(const AutoFilter &filter);

// One xl/tables/tableN.xml part for a TableSpec (id is workbook-wide and
// assigned by the post-pass).
[[nodiscard]] QByteArray tablePartXml(const TableSpec &table, int tableId);

// The legacy VML drawing Excel draws a sheet's notes from (M61, D-069): one
// hidden note shape per commented cell. `number` is N of
// xl/drawings/vmlDrawingN.vml and the first of the drawing's blocks of 1024
// shape ids; a drawing with more notes uses the following blocks too, so
// the next drawing's number must start after them (buildXlsx counts).
[[nodiscard]] QByteArray commentsVmlXml(const QList<CellAddr> &cells, int number);

} // namespace QindaOffice::Calc::IO
