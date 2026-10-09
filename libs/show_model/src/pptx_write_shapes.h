// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Model shapes → PresentationML for the pptx writer (internal; D-070).
// Three paths per shape, decided by its provenance (pptx_origin.h):
//   verbatim  — the content equals what the reader produced: the source
//               element, relationship ids carried, its id kept;
//   patched   — read from the deck but edited: the source element with
//               only the changed aspects rewritten (geometry, outline, fill,
//               text, picture, table cells, group members), so effects,
//               3-D, extensions and every unmodelled attribute survive;
//   generated — QindaShow's own shapes (and anything whose source cannot
//               be resolved): spelled from the model.
// Content QindaShow cannot spell itself (a chart or preserved element
// whose source part is gone) is written as a picture of its rendering.
//
// Files: pptx_write_shapes.cpp (dispatch, verbatim, patching),
// pptx_write_generate.cpp (generation, the picture fallback),
// pptx_write_table.cpp (a:tbl, M67), pptx_write_chart.cpp,
// pptx_write_chart_series.cpp and pptx_write_chart_kind.cpp (edited charts,
// M67, M82).

#include "pptx_write_text.h"

namespace QindaOffice::Show::Pptx {

// Reserves the Office ids `shapes` keep on their slide; returns, per shape,
// whether it keeps its own id (the rest get fresh ones when written).
[[nodiscard]] QList<bool> reserveShapeIds(PartWriter &out, const QList<Shape> &shapes);

// One shape with the given Office id; nullptr when nothing can be written.
[[nodiscard]] Ooxml::XmlElementPtr writeShape(PartWriter &out, const Shape &shape, quint32 id);

// ---- shared with pptx_write_generate.cpp ----
[[nodiscard]] Ooxml::XmlElementPtr generateShape(PartWriter &out, const Shape &shape, quint32 id);
[[nodiscard]] Ooxml::XmlElementPtr tableElement(PartWriter &out, const Table &table, const Ooxml::XmlElement *source,
                                                const Table *pristine, const QString &sourcePart);
[[nodiscard]] Ooxml::XmlElementPtr pictureFallback(PartWriter &out, const Shape &shape, quint32 id);
// M67 (D-076), pptx_write_chart.cpp: a new chart part holding `chart`,
// written over the source chart part whose cached data the reader made
// `previous` of (series data, formulas, caches, the kind, the embedded
// workbook); returns the relationship id from `out`'s part to it, empty
// when the source part cannot be used (the caller writes a picture).
[[nodiscard]] QString writeEditedChart(PartWriter &out, const QString &sourceChartPart, const ChartData &chart,
                                       const ChartData &previous);
// An edited chart's graphic frame: its c:chart reference taken off BEFORE
// the frame is carried (or the copier would drag the stale chart part and
// its workbook into the package), then pointed at the new part.
struct DetachedChart {
    Ooxml::XmlElement *reference = nullptr;
    QString sourcePart; // the source chart part ("" = cannot be resolved)
    [[nodiscard]] bool active() const { return reference != nullptr; }
};
[[nodiscard]] DetachedChart detachChart(const PartWriter &out, Ooxml::XmlElement &frame, const QString &part);
// For a chart frame copied without provenance (pasted): detached only
// when its data differs from what its source part caches (`cached`).
[[nodiscard]] DetachedChart detachEditedCopy(PartWriter &out, Ooxml::XmlElement &frame, const QString &part,
                                             const ChartData &chart, ChartData *cached);
// Writes the edited chart part and names it in the frame; false when the
// source chart part could not be used (the caller writes a picture).
[[nodiscard]] bool attachChart(PartWriter &out, const DetachedChart &edit, const ChartData &chart, const ChartData &previous);
// pptx_write_chart_kind.cpp: the plot `plot` of `plotArea` made `chart`'s
// kind (its type and grouping), with the axes it needs; the plot element
// now in place.
Ooxml::XmlElement *convertPlot(Ooxml::XmlElement &plotArea, Ooxml::XmlElement *plot, const ChartData &chart);
// a:xfrm for a shape (rot/flips; chOff/chExt for groups when `child` is set).
[[nodiscard]] Ooxml::XmlElementPtr transformElement(const QString &ns, const Shape &shape, bool group);
// The normalised rect and flips a model line is written with (pre-M62
// lines carry their direction in negative extents).
void lineGeometry(const Shape &shape, QRectF *rect, bool *flipH, bool *flipV);
[[nodiscard]] QString defaultName(const Shape &shape, quint32 id);

} // namespace QindaOffice::Show::Pptx
