// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// p:spTree → model shapes (pptx reader, internal; D-070): p:sp, p:pic,
// p:cxnSp, p:grpSp, p:graphicFrame (tables, charts, SmartArt, OLE),
// mc:AlternateContent, p:contentPart and anything unknown (kept as a
// Preserved shape holding its XML). Placeholders inherit geometry, body
// properties, fill, outline and text style from the layout's and the
// master's placeholder (PlaceholderSet), text from the master's
// title/body/other style (MasterTextStyles).
//
// Files: pptx_read_shapes.cpp (dispatch, p:sp, p:pic, p:cxnSp, p:grpSp),
// pptx_read_frames.cpp (graphic frames, alternate content, SmartArt),
// pptx_read_table.cpp, pptx_read_chart.cpp.

#include "pptx_context.h"

#include <QSet>

namespace QindaOffice::Show {
struct PptxReadReport;
}

namespace QindaOffice::Show::Pptx {

struct PlaceholderTemplate {
    Placeholder ph;
    Ooxml::XmlElementPtr element; // the p:sp on the layout / master
    PartContext ctx;              // where its colours and parts resolve
    bool hasXfrm = false;
    QRectF rect;
    qreal rotation = 0;
    bool flipH = false;
    bool flipV = false;
    ListStyle lstStyle;
};

class PlaceholderSet {
public:
    void add(PlaceholderTemplate item) { m_items.append(std::move(item)); }
    // The layout match for a slide placeholder: by index, then by type.
    [[nodiscard]] const PlaceholderTemplate *match(const Placeholder &ph) const;
    // The master match: by type (title, body, dt, ftr, sldNum).
    [[nodiscard]] const PlaceholderTemplate *matchType(const QString &type) const;

private:
    QList<PlaceholderTemplate> m_items;
};

struct MasterTextStyles {
    ListStyle base;  // presentation defaultTextStyle
    ListStyle title; // base + p:titleStyle
    ListStyle body;  // base + p:bodyStyle
    ListStyle other; // base + p:otherStyle
    [[nodiscard]] const ListStyle &forPlaceholder(const QString &type) const;
};

// Table styles of the deck (ppt/tableStyles.xml), by styleId.
struct TableStyles {
    Ooxml::XmlElementPtr root;
};

struct ShapeScope {
    PartContext ctx;
    const PlaceholderSet *layout = nullptr;
    const PlaceholderSet *master = nullptr;
    const MasterTextStyles *styles = nullptr;
    const TableStyles *tableStyles = nullptr;
    PptxReadReport *report = nullptr;
    QString idPrefix = QStringLiteral("sp");
    QSet<QString> *usedIds = nullptr;
    bool keepOrigin = true; // record provenance for the writer
};

// The shapes of a p:spTree / p:grpSp / dsp:spTree, in z order.
[[nodiscard]] QList<Shape> readShapeTree(const ShapeScope &scope, const Ooxml::XmlElement &tree,
                                         const ResolvedFill *groupFill = nullptr);
// The placeholders of a layout's or master's p:spTree.
[[nodiscard]] PlaceholderSet collectPlaceholders(const PartContext &ctx, const Ooxml::XmlElement &spTree);

// ---- shared with pptx_read_frames.cpp / _table / _chart ----
[[nodiscard]] Shape readElement(const ShapeScope &scope, const Ooxml::XmlElementPtr &element,
                                const ResolvedFill *groupFill);
void readNonVisual(const ShapeScope &scope, const Ooxml::XmlElement *cNvPr, Shape &shape);
// a:xfrm / p:xfrm: rect, rotation, flips; false when absent.
bool readTransform(const Ooxml::XmlElement *xfrm, Shape &shape);
[[nodiscard]] Shape readGraphicFrame(const ShapeScope &scope, const Ooxml::XmlElementPtr &element);
[[nodiscard]] Shape readAlternateContent(const ShapeScope &scope, const Ooxml::XmlElementPtr &element,
                                         const ResolvedFill *groupFill);
[[nodiscard]] Shape readPreserved(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const QString &label);
[[nodiscard]] Table readTable(const ShapeScope &scope, const Ooxml::XmlElement &tbl);
[[nodiscard]] ChartData readChart(const PartContext &slideCtx, const QString &chartPart);
// Stamps provenance (source part, element, pristine copy) on a finished shape.
void recordOrigin(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, Shape &shape);
// A unique model id for an Office shape id within the scope.
[[nodiscard]] QString shapeIdFor(const ShapeScope &scope, quint32 nvId);
void countShape(const ShapeScope &scope, const Shape &shape);
// M91 (D-099, pptx_media_edits.cpp): a medium's Trim, Fade Duration and
// bookmarks from its p14:media (nullptr: none).
void readMediaEdits(const Ooxml::XmlElement *media, MediaInfo &info);

} // namespace QindaOffice::Show::Pptx
