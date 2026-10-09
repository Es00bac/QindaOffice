// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The parts the pptx writer generates when a deck brings none of its own
// (internal; D-070): a theme, a slide master with title and body
// placeholders and the three layouts QindaShow's slides map onto (Title
// Slide, Title and Content, Blank), a notes master with its theme, notes
// slides, and the presentation-level presProps / viewProps / tableStyles.
// Every part is written to validate against the ECMA-376 Transitional
// schemas and to satisfy PowerPoint's own checks (ids, relationships).

#include "pptx_write_context.h"

#include <tuple>

namespace QindaOffice::Show::Pptx {

struct GeneratedMaster {
    QString part;
    QString themePart;
    quint32 masterId = 0;
    QString titleLayout;   // "title" slides
    QString contentLayout; // everything with a body
    QString blankLayout;   // "blank" slides
    // The model's own layouts of this master, generated: model id, type, part.
    QList<std::tuple<QString, QString, QString>> modelLayouts;
};

// A master (with its theme and three layouts) for a QindaShow master, or
// for plain white slides when `master` is null. `nextId` hands out
// sldMasterId / sldLayoutId values (≥ 2147483648, unique deck-wide).
// A model master that carries decorations or a styled background (an Office
// master whose source part is gone, e.g. a deck that went through .fodp)
// gets them written, and each model layout of it becomes a layout part of
// its type with its background and decorations.
[[nodiscard]] GeneratedMaster generateMaster(WriteSession &session, const SlideMaster *master, quint32 *nextId);

// A notes master and its theme; returns the part.
[[nodiscard]] QString generateNotesMaster(WriteSession &session);
// A notes slide holding `notes` (one paragraph per line).
[[nodiscard]] Ooxml::XmlElementPtr notesSlideElement(const QString &notes);

// presProps, viewProps and tableStyles; adds them and returns their parts.
struct PresentationParts {
    QString presProps;
    QString viewProps;
    QString tableStyles;
};
[[nodiscard]] PresentationParts generatePresentationParts(WriteSession &session);

// The presentation-wide default text style (presentation.xml).
[[nodiscard]] Ooxml::XmlElementPtr defaultTextStyleElement();

} // namespace QindaOffice::Show::Pptx
