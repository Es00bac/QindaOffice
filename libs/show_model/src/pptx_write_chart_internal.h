// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the pptx writer's edited charts (M67, D-076; M82, D-091):
// pptx_write_chart.cpp writes the part, pptx_write_chart_series.cpp the
// series of every plot, pptx_write_chart_kind.cpp a plot's kind change.

#include "chart_workbook.h"
#include "pptx_write_shapes.h"

namespace QindaOffice::Show::Pptx::ChartXml {

// Where the data sits and the number formats it is cached with, from the
// first series as the file has it.
struct DataShape {
    ChartWorkbook::Layout layout;
    QString categoryFormat; // formatCode of numeric categories (null = strings)
    QString valueFormat;
};

[[nodiscard]] Ooxml::XmlElementPtr make(const QString &name);
[[nodiscard]] Ooxml::XmlElementPtr valued(const QString &name, const QString &value);
// The plots of a plot area (c:barChart, c:lineChart, …), in order — the
// order the reader numbers ChartSeries::plot by.
[[nodiscard]] QList<Ooxml::XmlElement *> plotsOf(Ooxml::XmlElement &plotArea);

// Every plot's series as `chart` now has them: a series is the file's by
// its c:idx (ChartSeries::sourceIdx), an added one a copy of its plot's
// last series (its accent advanced), a removed one dropped; each one's
// name, categories — multi-level ones with their outer levels — and values
// rewritten (formulas for `shape`'s layout, caches), and its per-point
// colours and data labels moved with their categories
// (ChartData::categorySource). False when a plot has no series to copy.
bool patchPlots(const QList<Ooxml::XmlElement *> &plots, const ChartData &chart, const DataShape &shape);

// Carried into a converted series or plot: c:dPt as they are, c:dLbls
// without a label position the new kind may not take (PowerPoint repairs
// a deck whose labels sit where the kind cannot put them).
[[nodiscard]] Ooxml::XmlElementPtr labelsForKind(const Ooxml::XmlElement &labels);

} // namespace QindaOffice::Show::Pptx::ChartXml
