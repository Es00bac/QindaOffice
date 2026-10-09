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
// pptx_write_generate.cpp (generation, tables, the picture fallback).

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
// a:xfrm for a shape (rot/flips; chOff/chExt for groups when `child` is set).
[[nodiscard]] Ooxml::XmlElementPtr transformElement(const QString &ns, const Shape &shape, bool group);
// The normalised rect and flips a model line is written with (pre-M62
// lines carry their direction in negative extents).
void lineGeometry(const Shape &shape, QRectF *rect, bool *flipH, bool *flipV);
[[nodiscard]] QString defaultName(const Shape &shape, quint32 id);

} // namespace QindaOffice::Show::Pptx
