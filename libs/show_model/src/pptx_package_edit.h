// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Editing a WRITTEN deck package where PowerPoint keeps things (internal to
// show_model; M82, D-091) — the slide commands of pptx_slides.h work on the
// package writePptxPackage() made and then read the deck back
// (pptx_design.h's readBackEdited), so every slide inherits from its layout
// exactly as PowerPoint will show it:
//  - presentation.xml's slide and master lists (PackageEdit);
//  - a slide's layout relationship;
//  - the placeholders of a layout and PowerPoint's spelling of an empty one
//    on a new slide (no position, no look of its own: all inherited).
//
// AGENT-GUARD: sldMasterId and sldLayoutId values share one id space
// (≥ 2147483648, unique deck-wide) and sldId values are ≥ 256 and unique —
// PowerPoint repairs a file that breaks either rule. PackageEdit hands out
// ids above every id the package uses.

#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QRectF>
#include <QStringList>

namespace QindaOffice::Show::Pptx {

inline constexpr char16_t kPresentationPart[] = u"ppt/presentation.xml";

[[nodiscard]] QString slideContentType();
[[nodiscard]] QString slideRelType(const char *name);

class PackageEdit {
public:
    explicit PackageEdit(Ooxml::PackageWriter &writer);

    [[nodiscard]] Ooxml::PackageWriter &writer() { return m_writer; }
    // A part parsed (nullptr when absent), and written back in place.
    [[nodiscard]] Ooxml::XmlElementPtr xml(const QString &part) const;
    void setXml(const QString &part, const Ooxml::XmlElement &root);

    // The slide parts, in the deck's order.
    [[nodiscard]] QStringList slideParts() const;
    // Adds `part` to the slide list at `index` (the end when out of range).
    void insertSlide(const QString &part, int index);
    // Adds master `part` to the master list (with a fresh id); takes one away.
    void addMaster(const QString &part);
    void removeMaster(const QString &part);
    [[nodiscard]] QStringList masterParts() const;
    // An sldMasterId / sldLayoutId value no part uses yet.
    [[nodiscard]] quint32 takeLayoutId();
    // presentation.xml back into the package (call once, last).
    void commit();

    // The layout a slide is related to; moving it to another.
    [[nodiscard]] QString layoutOf(const QString &slidePart) const;
    void setLayout(const QString &slidePart, const QString &layoutPart);

private:
    [[nodiscard]] Ooxml::XmlElement &list(const char16_t *name);

    Ooxml::PackageWriter &m_writer;
    Ooxml::XmlElementPtr m_presentation;
    quint32 m_nextLayoutId = 2147483648u;
};

// One placeholder of a layout (or slide) as its p:ph says.
struct PlaceholderRef {
    QString type; // "obj" when the file leaves it out
    qint64 index = -1;
    QString size;
    QString orient;
    QString name;
    QRectF rectEmu; // its own a:xfrm (empty when it inherits)
    [[nodiscard]] bool isFooter() const;
    [[nodiscard]] bool isTitle() const;
};
[[nodiscard]] PlaceholderRef placeholderOf(const Ooxml::XmlElement &shape);
// A shape's p:ph, or nullptr.
[[nodiscard]] Ooxml::XmlElement *phElement(Ooxml::XmlElement &shape);
// The placeholders of a layout (or slide) part's shape tree.
[[nodiscard]] QList<PlaceholderRef> placeholdersOf(const Ooxml::XmlElement &part);
// The slide's shape tree.
[[nodiscard]] Ooxml::XmlElement *shapeTree(Ooxml::XmlElement &sld);

// Which of `candidates` a slide placeholder `ph` belongs to, as PowerPoint
// matches them: the same index (of a compatible kind), else the same type
// (titles with titles), else any content placeholder; `taken` are already
// matched. -1 when none.
[[nodiscard]] int matchPlaceholder(const PlaceholderRef &ph, const QList<PlaceholderRef> &candidates,
                                   const QList<int> &taken);

// An empty placeholder as PowerPoint writes one on a new slide: its p:ph,
// no position or look of its own, one empty paragraph.
[[nodiscard]] Ooxml::XmlElementPtr emptyPlaceholder(const PlaceholderRef &ph, quint32 id);
// A new slide part's root holding `shapes` (p:sld, an empty group, the
// master's colour mapping).
[[nodiscard]] Ooxml::XmlElementPtr newSlideRoot(const QList<Ooxml::XmlElementPtr> &shapes);
// The largest p:cNvPr id of a shape tree.
[[nodiscard]] quint32 largestShapeId(const Ooxml::XmlElement &tree);
// Gives a slide root a fresh p14:creationId (added when it has none).
void renewCreationId(Ooxml::XmlElement &sld);

} // namespace QindaOffice::Show::Pptx
