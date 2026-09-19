// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Chart part generators for the post-pass: a whole chart part for a
// series-authored ChartSpec (QXlsx's chart API only expresses one
// contiguous range, so imported multi-range charts cannot go through it),
// and the graphicFrame anchor that places it in a sheet's drawing.

#include "chart_spec.h"

#include <QByteArray>
#include <QString>

namespace QindaOffice::Calc::IO {

// A chart part (c:chartSpace) from a ChartSpec with a non-empty series list.
[[nodiscard]] QByteArray chartPartXml(const ChartSpec &spec);

// A twoCellAnchor graphicFrame referencing chart relationship `rid`.
[[nodiscard]] QByteArray graphicFrameAnchorXml(const QString &rid, int cNvPrId,
                                               int fromCol, int fromRow, int toCol, int toRow);

} // namespace QindaOffice::Calc::IO
