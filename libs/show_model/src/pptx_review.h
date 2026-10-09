// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sections and comments between PowerPoint and the model (internal to
// show_model; M75, D-084).
//
// Sections are p14:sectionLst in presentation.xml's extension list
// (uri {521415D9-36F7-43E2-AB2F-B90AF26B5E84}): named runs of slides by
// p:sldId/@id. The writer always spells the model's sections (none = no
// list), with the slide ids it wrote.
//
// Comments come in PowerPoint's two kinds. Modern (PowerPoint 365 / 2021):
// one ppt/comments/modernComment_<slide id>_<creation id>.xml per slide
// (relationship .../2018/10/relationships/comments, named from the slide's
// p188:commentRel extension), p188:cm threads with p188:replyLst, anchored
// to the slide (pc:sldMkLst) or a shape (ac:deMkLst/ac:spMk by its id),
// authors in ppt/authors.xml (GUIDs). Legacy: ppt/comments/commentN.xml
// (p:cmLst/p:cm with p:pos in 1/8 pt), replies through p15:threadingInfo,
// authors in ppt/commentAuthors.xml (numbers, lastIdx). A slide whose
// comments are unchanged keeps its comments part byte for byte; the author
// lists keep every author's id.
//
// AGENT-CONTRACT: the reader (pptx_read_review.cpp) and the writer
// (pptx_write_review.cpp) agree on these names; the slide writer
// (pptx_write_slide.cpp) leaves the comment relationships and the
// p188:commentRel extension to writeSlideComments().

#include "pptx_read_masters.h"
#include "pptx_write_context.h"

namespace QindaOffice::Show::Pptx {

[[nodiscard]] const QString &nsP14();
[[nodiscard]] const QString &nsP15();
[[nodiscard]] const QString &nsP188();
[[nodiscard]] const QString &nsPc();
[[nodiscard]] const QString &nsAc();
[[nodiscard]] const QString &nsA16();

inline const char16_t kSectionsUri[] = u"{521415D9-36F7-43E2-AB2F-B90AF26B5E84}";
inline const char16_t kCreationIdUri[] = u"{BB962C8B-B14F-4D97-AF65-F5344CB8AC3E}";
inline const char16_t kCommentRelUri[] = u"{6950BFC3-D8DA-4A85-94F7-54DA5524770B}";
inline const char16_t kThreadingUri[] = u"{C676402C-5697-4E1C-873F-D02D1690AC5C}";
inline const char16_t kShapeCreationUri[] = u"{FF2B5EF4-FFF2-40B4-BE49-F238E27FC236}";

[[nodiscard]] QString modernCommentsRelType();
[[nodiscard]] QString modernAuthorsRelType();
[[nodiscard]] bool isCommentsRelType(const QString &type);
[[nodiscard]] bool isAuthorsRelType(const QString &type);

// ---- reading ----
// The sections of presentation.xml, slide ids mapped to the model's.
[[nodiscard]] QList<SlideSection> readSections(const DeckReadState &state);
// The comments of one slide (legacy or modern); fills the slide's comments
// and its source's comment fields. `shapeIds` maps the slide's Office
// shape ids to model ids (shape anchors).
void readSlideComments(const DeckReadState &state, const QString &slidePart, const QHash<quint32, QString> &shapeIds,
                       Slide &slide, SlideSource &source);
// The slide's p14:creationId (0 = none).
[[nodiscard]] quint32 slideCreationId(const Ooxml::XmlElement &sld);

// ---- writing ----
// The deck's comment authors and what was carried, for one save.
struct CommentState {
    bool modern = true;
    QHash<QString, QString> authorIds;       // author name → id written
    QHash<QString, QString> initials;        // author name → initials
    QHash<QString, int> lastIndex;           // legacy: author id → last idx used
    QList<std::pair<QString, QString>> order; // id, name — the author list in order
    bool anyChanged = false;                 // some slide's comments differ from the file's
};
// Decides the kind and collects the authors (the source's first, ids kept).
[[nodiscard]] CommentState prepareComments(const WriteSession &session);
// Writes (or carries) one slide's comments: the comments part, the slide's
// relationship to it and, for modern ones, the p188:commentRel extension
// and the slide's p14:creationId. `writtenIds` maps model shape ids to the
// ids just written on the slide; `sldId` is the slide's id in presentation.xml.
void writeSlideComments(PartWriter &out, Ooxml::XmlElement &sld, const Slide &slide, quint32 sldId,
                        const QHash<QString, quint32> &writtenIds, CommentState &comments);
// The author list part(s), related from presentation.xml (none when no
// slide has comments).
void writeCommentAuthors(PartWriter &presentation, const CommentState &comments);
// presentation.xml's p14:sectionLst for `sections` (nullptr for none).
[[nodiscard]] Ooxml::XmlElementPtr sectionsExtension(const QList<SlideSection> &sections,
                                                     const QHash<QString, quint32> &slideIds);

} // namespace QindaOffice::Show::Pptx
