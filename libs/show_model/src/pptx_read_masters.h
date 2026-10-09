// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pptx reader's deck-wide state and its masters and layouts (internal
// to show_model; D-070, M75 D-084). A master is read with its theme (as
// the model's ThemeSpec and as the context colours resolve through), its
// colour map, its text styles (the presentation's default text style under
// p:titleStyle / p:bodyStyle / p:otherStyle — kept as resolution input and
// as the model's per-level styles), its placeholders and decorations, and
// then its layouts in its own order; each layout with its placeholders
// (inheriting the master's) and decorations. Both get a pristine snapshot
// (pptx_origin.h) so the writer carries the unedited ones byte for byte.
//
// Files: pptx_read_masters.cpp (this), pptx_reader.cpp (slides, the deck),
// pptx_read_review.cpp (sections and comments).

#include "pptx_read_shapes.h"

#include <deque>
#include <memory>

namespace QindaOffice::Show {
struct PptxReadReport;
}

namespace QindaOffice::Show::Pptx {

struct MasterInfo {
    QString part;
    QString id;
    std::shared_ptr<ThemeContext> theme;
    Ooxml::ColorMap map;
    MasterTextStyles styles;
    PlaceholderSet placeholders;
};

struct LayoutInfo {
    QString part;
    QString id;
    int master = -1;
    Ooxml::ColorMap map;
    PlaceholderSet placeholders;
};

struct DeckReadState {
    std::shared_ptr<const Ooxml::Package> package;
    PptxReadReport *report = nullptr;
    QString main;
    Ooxml::XmlElementPtr presentation;
    QHash<QString, QString> slideIds; // slide part → model id
    std::deque<MasterInfo> masters;
    std::deque<LayoutInfo> layouts;
    QHash<QString, int> layoutByPart;
    TableStyles tableStyles;

    [[nodiscard]] PartContext context(const QString &part, const MasterInfo &master, const Ooxml::ColorMap &map) const;
};

// The colour map an element spells (p:clrMap, a:overrideClrMapping).
[[nodiscard]] Ooxml::ColorMap colorMapOf(const Ooxml::XmlElement *element);
// p:clrMapOvr: a:overrideClrMapping replaces the map, masterClrMapping keeps it.
[[nodiscard]] Ooxml::ColorMap overriddenMap(const Ooxml::ColorMap &base, const Ooxml::XmlElement *root);
// A part's p:cSld/p:bg as the model's Background.
[[nodiscard]] Background backgroundOf(const PartContext &ctx, const Ooxml::XmlElement *cSld);
// The nine levels of a text style as effective paragraphs (one empty run
// each carrying the level's run look).
[[nodiscard]] QList<TextParagraph> levelStyles(const PartContext &ctx, const ListStyle &style);

// Reads the master at `part` and then its layouts; false when unreadable.
bool readMaster(DeckReadState &state, const QString &part, quint32 masterId, Presentation &deck);

} // namespace QindaOffice::Show::Pptx
