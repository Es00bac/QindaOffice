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
// M91 (D-099): the Effect Options direction (presetSubtype; 0 = the
// effect's default) and exit effects (`exit`, presetClass="exit").
struct BuildEntry {
    quint32 shapeId = 0;
    int step = 0;
    ShapeAnimation animation = ShapeAnimation::Appear;
    int durationMs = 0;
    int direction = 0;
    bool exit = false;
    bool operator==(const BuildEntry &) const = default;
};

// M75 (D-084): a media shape's playback as p:timing spells it — the click
// that plays it (0: none), whether it starts with the slide, its media
// node's loop, visibility when stopped and volume.
struct MediaEntry {
    quint32 shapeId = 0;
    MediaKind kind = MediaKind::Video;
    int step = 0;
    bool autoPlay = false;
    bool loop = false;
    bool hideWhenStopped = false;
    int volume = 80; // percent
    int durationMs = 0;
    bool narration = false;
    bool operator==(const MediaEntry &) const = default;
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
    QList<MediaEntry> media;             // M75: the media playback it mapped from p:timing
    QString notesPart;                   // its notes slide ("" = none)
    QString notes;                       // the notes text as read
    QList<TextParagraph> notesParagraphs; // the notes body as read (M67: an edit reuses its spelling)
    Background background;               // the slide's own background as read
    QString layout;                      // the QindaShow layout name as read
    // M75 (D-084): the comments as read, the parts they came from (one
    // legacy or modern comments part) and the slide's p14:creationId, which
    // modern comments name it by (0 = none).
    QList<SlideComment> comments;
    QString commentsPart;
    bool modernComments = false;
    quint32 creationId = 0;
    // M91 (D-099): Advance Slide as read (p:transition @advTm / @advClick).
    int advanceAfterMs = -1;
    bool advanceOnClick = true;
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
    // M75 (D-084): the master as the reader made it (its own origin empty)
    // — a master equal to it is carried byte for byte — and its theme part.
    SlideMaster pristine;
    QString themePart;
};

struct LayoutSource {
    QString part;
    SlideLayout pristine; // M75: the layout as read (its own origin empty)
};

struct DeckSource {
    std::shared_ptr<const Ooxml::Package> package;
    QString presentationPart;          // "ppt/presentation.xml"
    Ooxml::XmlElementPtr presentation; // p:presentation as read
    QHash<QString, QString> slideIds;  // source slide part → model slide id
    // M91 (D-099): the show settings as read (presProps.xml's p:showPr —
    // carried byte for byte while the model still equals them) and its part.
    ShowSettings showSettings = ShowSettings::fileDefaults();
    QString presPropsPart;
};

} // namespace QindaOffice::Show
