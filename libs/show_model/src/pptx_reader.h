// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native PowerPoint (.pptx) reader (M62, D-070, D-068). Maps
// PresentationML onto the show_model: slides with every shape kind,
// masters and layouts (backgrounds, decorative shapes, placeholder
// inheritance of geometry and text style), themes and colour maps, tables,
// charts from cached values, notes, hidden slides, transitions and the
// main sequence's builds. Anything QindaShow cannot model becomes a
// Preserved shape holding its XML and source part; the deck keeps the
// package it came from so the writer can carry the rest across.
//
// Ownership: the returned Presentation shares the (immutable) package
// through its provenance (pptx_origin.h). Threading: pure function.

#include "presentation.h"

#include <QMap>
#include <QStringList>

#include <memory>

namespace QindaOffice::Ooxml {
class Package;
}

namespace QindaOffice::Show {

struct PptxReadReport {
    int slides = 0;
    int shapes = 0;    // every shape, group members included
    int preserved = 0; // shapes kept as XML (SmartArt, media, OLE, ink, …)
    QMap<QString, int> kinds;
    QStringList warnings;
};

[[nodiscard]] Presentation readPptx(const std::shared_ptr<const Ooxml::Package> &package, QString *error = nullptr,
                                    PptxReadReport *report = nullptr);
[[nodiscard]] Presentation readPptxFile(const QString &path, QString *error = nullptr, PptxReadReport *report = nullptr);
[[nodiscard]] Presentation readPptxData(const QByteArray &zip, QString *error = nullptr, PptxReadReport *report = nullptr);

// True when `zip` is an OPC package (starts like a zip).
[[nodiscard]] bool looksLikeZip(const QByteArray &bytes);

// The look a NEW table on `slide` gets (M67, D-076), as PowerPoint picks
// it: the deck's default table style (tableStyles.xml/@def; PowerPoint's
// Medium Style 2 – Accent 1 when the deck names none or one it does not
// define) resolved against the theme of the slide's master. A deck that was
// not read from .pptx resolves against the Office theme the writer
// generates for it. `textColor` and `fontFamily` are the deck's plain text
// (tx1, the minor font) that cells outside any styled region show.
struct NewTableStyle {
    QString styleId;
    TableLook look;
    QColor textColor;
    QString fontFamily;
};
[[nodiscard]] NewTableStyle newTableStyle(const Presentation &deck, const Slide &slide);

} // namespace QindaOffice::Show
