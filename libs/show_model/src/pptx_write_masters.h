// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Slide masters, their layouts and themes for the pptx writer (internal;
// D-070, M75 D-084). Three paths per master, as for shapes:
//   carried   — the master, every layout of it and its theme are what the
//               reader produced: the parts are copied byte for byte;
//   edited    — the Slide Master view (or Design ▸ Themes, the Masters
//               dialog) changed something: the master part is the source's
//               with the changes written in (background, text styles,
//               placeholders and decorations through the shape writer, its
//               name, the layout list in the model's order), each layout
//               carried byte for byte when unedited, patched when edited
//               and generated when new; the theme part copied, patched
//               (colours, fonts, effects) or replaced by one from another
//               file;
//   generated — no source: pptx_write_parts.h's generateMaster.
// Ids follow PowerPoint's rules: sldMasterId / sldLayoutId ≥ 2147483648,
// unique deck-wide, kept where they were.

#include "pptx_write_parts.h"

#include <tuple>

namespace QindaOffice::Show::Pptx {

struct MasterOut {
    QString modelId;
    QString part;
    quint32 masterId = 0;
    bool generated = false;
    GeneratedMaster made;
    QList<std::tuple<QString, QString, QString>> layouts; // written: model id, type, part
};

// Every model master (in the deck's order), carried, edited or generated.
// `nextId` hands out sldMasterId / sldLayoutId values.
[[nodiscard]] QList<MasterOut> writeMasters(WriteSession &session, quint32 *nextId);

// One shape of a master or layout: placeholders keep their look in the
// list styles (pptx_write_styles.h) — `stylesElsewhere` for a master's
// title and body, whose look is the master's text styles.
[[nodiscard]] Ooxml::XmlElementPtr writeTemplateShape(PartWriter &out, const Shape &shape, quint32 id,
                                                      bool stylesElsewhere, const ThemeFonts &fonts);

} // namespace QindaOffice::Show::Pptx
