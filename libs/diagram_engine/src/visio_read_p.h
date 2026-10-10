// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio reader (M65, D-073): the context shared by the
// package-level reader (visio_read.cpp) and the shape reader
// (visio_read_shape.cpp).
//
// Coordinates: a shape's LOCAL space is inches with y up and the origin at
// the bottom-left of its box; `toPage` transforms map local inches to page
// units (1/96 in, y down), composing every enclosing group's transform and
// the page's drawing scale.

#include "visio_doc.h"
#include "visio_geometry.h"
#include "visio_source.h"
#include "visio_text.h"

#include <QSet>
#include <QTransform>

namespace QindaOffice::Diagram::Visio {

struct MasterInfo {
    int id = -1;
    QString part;
    XmlElementPtr root;
    QHash<int, const XmlElement *> shapes; // every shape of the master by ID
    int first = -1;                        // its first top-level shape
    QHash<int, Sheet> sheets;              // resolved on first use
    [[nodiscard]] const Sheet *sheet(int id);
};

struct ReadContext {
    std::shared_ptr<const Ooxml::Package> package;
    DocumentInfo doc;
    QHash<int, MasterInfo> masters;
    Drawing *drawing = nullptr;
    Source *source = nullptr;
    // Tallies for the warnings.
    int metafiles = 0;
    int objects = 0;
    int kept = 0;
    int richText = 0;
};

// One page: shapes (recursively through groups) into the model page, the
// source page's bookkeeping, then the page's <Connects> as glue.
class PageReader {
public:
    PageReader(ReadContext &ctx, SourcePage &source, Page &page, const QTransform &pageTransform, int variation);

    // The Visio IDs the page's <Connects> glue to (read before the shapes).
    void prescan(const XmlElement *connects);
    void readShapes(const XmlElement *shapes);
    void readConnects(const XmlElement *connects);
    // Records every item's final value as its snapshot (call last).
    void snapshot();

private:
    struct Parent {
        QTransform toPage;
        MasterInfo *master = nullptr; // the instance master of the enclosing group
        QString groupKey;             // top-level source group
        QString groupId;              // model group id
    };

    void readShape(const XmlElementPtr &element, const Parent &parent, int depth);
    void readGroup(const XmlElementPtr &element, const Sheet &sheet, const Parent &parent, const QTransform &local,
                   const QString &key, int depth, const MasterInfo *base, int masterShape);
    // Model shape from a 2-D sheet (or a filled 1-D one) placed by `toPage`;
    // false when nothing of it would show (unless `force`: a glue target).
    // `master` is the instance master its pictures may live in.
    bool makeShape(const XmlElementPtr &element, const Sheet &sheet, const QTransform &toPage,
                   const QString &groupId, const MasterInfo *master, const QString &key, bool force);
    void makeConnector(const XmlElementPtr &element, const Sheet &sheet, const Parent &parent, const QString &key);
    // `master`/`masterShape`: what the sheet inherits from (null: nothing).
    void record(const XmlElementPtr &element, const QString &key, const Parent &parent, int visioId, bool groupSheet,
                const MasterInfo *master, int masterShape);
    void styleShape(const Lookup &lookup, Shape &shape) const;
    void readData(const Sheet &sheet, QList<DataField> &data) const;

    ReadContext &m_ctx;
    SourcePage &m_source;
    Page &m_page;
    QTransform m_pageTransform;
    int m_variation;
    QSet<int> m_glueTargets;          // Visio IDs a Connect glues to
    QHash<int, QString> m_modelIds;   // Visio ID → model shape/connector id
    QHash<QString, QString> m_keyOf;  // model id → item key
    // Connector id → BeginX, BegTrigger, EndX, EndTrigger formulas.
    QHash<QString, QStringList> m_glueFormulas;
};

// ---- visio_read_m87.cpp (M87, D-095) ----

// The PageSheet's layers, line jumps, print setup and Auto Size.
void readPageSettings(const DocumentInfo &doc, const Sheet &pageSheet, Page &page);
// An item's layers (LayerMember) and hyperlinks (the Hyperlink section).
void readItemExtras(const Sheet &sheet, QList<int> &layers, QList<Hyperlink> &links);
// Visio's container kind and behaviour (User.msvStructureType …) of a shape.
void readContainer(const Sheet &sheet, double pxPerInch, Shape &shape);
// The document's comments part onto the pages and items it names; call
// once every page is read. Returns how many comments were read.
int readComments(ReadContext &ctx, const QString &documentPart);

// ---- visio_read_style.cpp: one sheet's cells as model values ----

// The transform of a sheet's local space into its parent's (both inches).
[[nodiscard]] QTransform localTransform(const Lookup &lookup);

// A model shape's box: `box` (local inches, y up: left/bottom, size) as
// placed by `toPage`, turned and mirrored the way the model draws it.
struct Frame {
    QRectF rect;
    double rotation = 0;
    bool flipX = false;
};
[[nodiscard]] Frame frameOf(const QTransform &toPage, const QRectF &box);
// Local inches inside `box` → 0..1 with y down (Shape::paths, textBox).
[[nodiscard]] QTransform normalizer(const QRectF &box);
// The box a sheet's shape spans locally: Width × Height, widened to its
// geometry where a side is empty (a line drawn as a 2-D shape).
[[nodiscard]] QRectF localBox(const Lookup &lookup, const QList<Figure> &figures, double pxPerInch);

void readShapeStyle(const Lookup &lookup, ShapeStyle &style);
void readLine(const Lookup &lookup, QColor &color, double &width, LinePattern &pattern);
[[nodiscard]] ArrowStyle arrowOf(int visioArrow);
// The text block (TxtPinX … minus the margins), normalized to `box`.
[[nodiscard]] QRectF textBoxOf(const Lookup &lookup, const QRectF &box);
// Visible shape data (the Property section) as model data fields.
[[nodiscard]] QList<DataField> shapeData(const Sheet &sheet);
// The Connection section's points normalized to `box`.
[[nodiscard]] QList<QPointF> connectionPoints(const Sheet &sheet, const QRectF &box);
// Black or white, whichever reads on `fill` (themed text colours).
[[nodiscard]] QColor readableOn(const QColor &fill);

} // namespace QindaOffice::Diagram::Visio
