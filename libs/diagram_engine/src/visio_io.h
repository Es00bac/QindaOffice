// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Microsoft Visio interchange for QindaDiagram (M65, D-073; the formats,
// the mapping and what is preserved are in docs/visio-interchange.md).
//
//  * read(): a Visio 2013+ package (.vsdx drawing, .vsdm macro-enabled
//    drawing, .vstx/.vstm template) becomes a Drawing — every foreground
//    and background page; shapes with their master and style inheritance
//    resolved, their geometry as custom outlines (Shape::paths), text,
//    fill, line, shape data and connection points; groups flattened to the
//    model's one level; 1-D connectors with their glue (the page's
//    <Connects>); pictures with their bytes.
//  * write(): a Drawing becomes a package Visio opens without repair. With
//    the Source it was read from (D-068), what the user did not change is
//    written back as it was — an unedited shape's <Shape> element verbatim,
//    the masters, style sheets, theme, layers, shape data, hyperlinks,
//    solutions and everything else the model does not hold — and changed
//    shapes keep every cell and section the model does not map.
//
// AGENT-CONTRACT (the model ⇄ the source): an item read from Visio carries
// extra["vsdx"] = {"src": <token>, "key": "<page id>/<shape id>"} and a
// page extra["vsdx"] = {"src", "page"}. The writer trusts a key only when
// the token is its source's; a copy pasted into another drawing, or kept
// in a .qdiagram after its source is gone, is written fresh.

#include "drawing.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace QindaOffice::Diagram::Visio {

struct Source; // the package a drawing was read from (visio_source.h)

enum class Kind { Drawing, MacroDrawing, Template, MacroTemplate };

struct ReadResult {
    Drawing drawing;
    std::shared_ptr<const Source> source;
    // What is shown approximately or kept without being shown, for the
    // status bar ("3 pictures in EMF format are kept but not shown").
    QStringList warnings;
};

// .vsdx .vsdm .vstx .vstm
[[nodiscard]] bool isVisioPath(const QString &path);
// .vsd .vss .vst .vdx .vsx .vtx: Visio 2003–2010 formats (not supported).
[[nodiscard]] bool isLegacyVisioPath(const QString &path);
[[nodiscard]] QString legacyFormatMessage(const QString &path);
[[nodiscard]] Kind kindForPath(const QString &path);

bool read(const QString &path, ReadResult *result, QString *error = nullptr);
bool readData(const QByteArray &zip, ReadResult *result, QString *error = nullptr);

// The package for `drawing`; `source` may be null (everything fresh).
[[nodiscard]] QByteArray write(const Drawing &drawing, const Source *source, Kind kind, QString *error = nullptr);
// write() saved atomically (QSaveFile); the kind follows the suffix.
bool save(const QString &path, const Drawing &drawing, const Source *source, QString *error = nullptr);

// The token of the package a source was read from, and the token items of
// `drawing` refer to (empty when none): DrawingDocument uses them to find
// the source again after crash recovery (the file must still match).
[[nodiscard]] QString sourceToken(const Source &source);
[[nodiscard]] QString referencedToken(const Drawing &drawing);

} // namespace QindaOffice::Diagram::Visio
