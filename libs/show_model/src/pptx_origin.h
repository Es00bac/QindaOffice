// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a deck read from .pptx remembers about its source (D-070, D-068
// rule 3), hung off the model through Provenance<> (presentation.h).
// The pptx writer re-emits the source markup of everything whose content
// still equals what the reader produced (the `pristine` snapshots), and
// carries the parts the model does not edit (masters, layouts, themes,
// presProps, viewProps, tableStyles, comments, embedded fonts) across with
// their relationships.
//
// AGENT-CONTRACT: show_model's pptx reader (pptx_reader.cpp) fills these
// and its writer (pptx_writer.cpp) consumes them. The XML trees are never
// mutated after reading (the writer clones before patching), and every
// `part` is the SOURCE part the element's relationship ids resolve
// against (Ooxml::RelationshipCopier::rewrite needs it).

#include "presentation.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <memory>

namespace QindaOffice::Show {

struct ShapeSource {
    QString part;               // the slide / layout / master the shape is on
    Ooxml::XmlElementPtr xml;   // p:sp, p:pic, p:cxnSp, p:grpSp, p:graphicFrame, …
    Shape pristine;             // the model as read (its own origin empty)
};

// A shape's build as read from p:timing (nvId → step/animation/duration).
struct BuildEntry {
    quint32 shapeId = 0;
    int step = 0;
    ShapeAnimation animation = ShapeAnimation::Appear;
    int durationMs = 0;
    bool operator==(const BuildEntry &) const = default;
};

struct SlideSource {
    QString part;                        // "ppt/slides/slide3.xml"
    Ooxml::XmlElementPtr xml;            // p:sld
    quint32 sldId = 0;                   // p:sldId/@id in presentation.xml
    QString layoutPart;                  // the layout it related to
    QString transition;                  // the mapped transition as read
    Ooxml::XmlElementPtr transitionXml;  // p:transition, or the mc:AlternateContent around it
    Ooxml::XmlElementPtr timingXml;      // p:timing
    QList<BuildEntry> builds;            // the builds the reader mapped from p:timing
    QString notesPart;                   // its notes slide ("" = none)
    QString notes;                       // the notes text as read
    Background background;               // the slide's own background as read
    QString layout;                      // the QindaShow layout name as read
    bool operator==(const SlideSource &) const = default;
};

struct MasterSource {
    QString part;
    quint32 masterId = 0; // p:sldMasterId/@id
    // The QindaShow-facing summary as read; a difference means the user
    // restyled the master in the Masters dialog.
    QColor background;
    QColor titleColor;
    QColor bodyColor;
    QString fontFamily;
};

struct LayoutSource {
    QString part;
};

struct DeckSource {
    std::shared_ptr<const Ooxml::Package> package;
    QString presentationPart;          // "ppt/presentation.xml"
    Ooxml::XmlElementPtr presentation; // p:presentation as read
    QHash<QString, QString> slideIds;  // source slide part → model slide id
};

} // namespace QindaOffice::Show
