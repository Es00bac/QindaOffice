// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio writer (M65, D-073): the package assembly
// (visio_write.cpp), one page's <PageContents> (visio_write_page.cpp), one
// shape's or connector's cells (visio_write_shape.cpp) and the parts a new
// document needs (visio_write_document.cpp).
//
// Coordinates: `toParent` transforms take PAGE units (1/96 in, y down) to
// the inches of the element's parent — the page (y up from the bottom, in
// drawing units) or the group it is written into (its local space).
//
// AGENT-GUARD (Visio's repair dialog): shape IDs are unique per page at
// every depth; a Connect names only IDs written on its page; a child naming
// a MasterShape is only ever written inside a group carrying its Master;
// names (NameU) are unique per page and page names per document. Every
// rule here exists because Visio "repairs" a file that breaks it.

#include "ooxml_copier.h"
#include "ooxml_package.h"
#include "visio_source.h"
#include "visio_xml.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QTransform>

namespace QindaOffice::Diagram::Visio {

struct WriteContext {
    const Drawing *drawing = nullptr;
    const Source *source = nullptr;
    Ooxml::PackageWriter *writer = nullptr;
    Ooxml::RelationshipCopier *copier = nullptr; // null without a source
    QHash<int, QString> masterParts;             // master ID → its written part
    int shapeStyle = 3;                          // style IDs fresh items use
    int connectorStyle = 3;
    QString defaultFont;                         // for a TextStyle without a family
};

// A connector end's glue as written: the target's Visio ID (0 = free)
// and its connection point (-1 = the outline, "dynamic glue").
struct GlueEnd {
    int sheet = 0;
    int port = -1;
};

// How a written element's parent frame relates to its source's: the same
// (perhaps shifted by `shift` inches, when the page grew or shrank and
// Visio's origin, the page bottom, moved), or a different parent, which
// rewrites every placement cell.
struct FrameChange {
    bool changed = false;
    QPointF shift;
};

// One page's <PageContents> written to `part`.
class PageWriter {
public:
    PageWriter(WriteContext &ctx, const Page &page, const SourcePage *source, const QString &part);
    [[nodiscard]] XmlElementPtr build();
    // Master IDs the written shapes use (the page needs a relationship each).
    [[nodiscard]] QSet<int> mastersUsed() const { return m_masters; }

private:
    enum class Mode { Verbatim, Merged, Fresh };
    struct Plan {
        Mode mode = Mode::Fresh;
        const SourceItem *item = nullptr;
        FrameChange frame{true, {}};
        int id = 0;
    };
    struct GroupPlan {
        const SourceGroup *group = nullptr; // the source group it is built from
        bool rigid = false;    // the source element as it was, shifted by `moved`
        QPointF moved;         // page units
        int id = 0;
        QStringList members;   // model ids, model order
    };

    // ---- visio_write_page.cpp: deciding ----
    void plan();
    void planGroups();
    void planItem(const QString &id, const QJsonObject &extra, const QString &groupId, bool unchangedItem);
    [[nodiscard]] const SourceItem *itemOf(const QJsonObject &extra) const;
    [[nodiscard]] bool unchanged(const Shape &shape, const SourceItem &item) const;
    [[nodiscard]] bool unchanged(const Connector &connector, const SourceItem &item) const;
    int freshId();

    // ---- visio_write_emit.cpp: writing ----
    XmlElementPtr writeItem(const QString &modelId, const QTransform &toParent);
    XmlElementPtr writeGroup(const QString &groupId);
    [[nodiscard]] GlueEnd glueOf(const Endpoint &end) const;
    void writeConnects(XmlElement &contents);
    XmlElementPtr cloneSource(const SourceItem &item);
    [[nodiscard]] const XmlElement *masterOf(const SourceItem &item) const;
    void noteMasters(const XmlElement &element);

    WriteContext &m_ctx;
    const Page &m_page;
    const SourcePage *m_source;
    QString m_part;
    QTransform m_toPage;                  // page units → page inches
    double m_scale = 1;                   // paper inches per drawing inch
    bool m_pageShifted = false;           // the page height changed
    QHash<QString, Plan> m_plans;         // model id → plan
    QHash<QString, GroupPlan> m_groups;   // model group id → plan
    QSet<QString> m_usedKeys;             // source items already written once
    QSet<int> m_reserved;                 // Visio IDs taken
    QSet<int> m_keptIds;                  // IDs written with their source identity
    QSet<int> m_verbatimIds;              // IDs written exactly as in the source
    QSet<int> m_generated;                // connectors whose <Connect> rows are new
    int m_nextId = 1;
    QHash<QString, int> m_visioIds;       // model id → written Visio ID
    QSet<QString> m_portsNeeded;          // shapes glued by a port they lack
    QSet<int> m_masters;
};

// ---- visio_write_shape.cpp ----

// A new <Shape> for a model shape.
[[nodiscard]] XmlElementPtr freshShape(WriteContext &ctx, const Shape &shape, int id, const QTransform &toParent,
                                       const QString &part, bool withPorts);
// Rewrites in `element` (a clone of the source <Shape>) the cells of what
// differs between `shape` and `snapshot`; `frame` says how its parent
// changed (a new parent rewrites every placement cell). `master` is the master shape the source
// inherits from (null: none); `withPorts` adds connection points a
// connector glues to when the shape has none.
void updateShape(WriteContext &ctx, XmlElement &element, const Shape &shape, const Shape &snapshot,
                 const QTransform &toParent, FrameChange frame, const QString &part, bool withPorts,
                 const XmlElement *master);
// A 1-D <Shape> along `route` (page units) for a model connector; glue
// ends name their targets' Visio IDs (0 = free) and ports.
[[nodiscard]] XmlElementPtr freshConnector(WriteContext &ctx, const Connector &connector, int id,
                                           const QVector<QPointF> &route, bool curved, const QTransform &toParent,
                                           GlueEnd begin, GlueEnd end);
void updateConnector(WriteContext &ctx, XmlElement &element, const Connector &connector, const Connector &snapshot,
                     const QVector<QPointF> &route, bool curved, const QTransform &toParent, GlueEnd begin,
                     GlueEnd end, bool routeChanged, const XmlElement *master);
// XForm cells of a group spanning `bounds` (page units) in its parent.
void setGroupFrame(XmlElement &group, const QRectF &bounds, const QTransform &toParent);
// The NameU/Name a fresh shape gets: "<base>.<id>" (Visio's own pattern).
void setShapeNames(XmlElement &element, const QString &base, int id);

// ---- visio_write_document.cpp ----

[[nodiscard]] XmlElementPtr freshDocument();
[[nodiscard]] XmlElementPtr freshWindows(const QString &firstPageId, QSizeF firstPageInches);
// docProps/custom.xml: `source` (or nothing) with RecalcDocument=true, which
// tells Visio to recompute every formula on open (the cached values of the
// cells this writer changed then agree with their formulas).
[[nodiscard]] XmlElementPtr customProperties(const XmlElement *source);
[[nodiscard]] XmlElementPtr freshPageEntry(int id, const QString &name, QSizeF inches);

} // namespace QindaOffice::Diagram::Visio
