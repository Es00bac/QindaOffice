// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DiagramService: the QML API of one QindaDiagram window (M50, D-050). A
// QVariant veneer over diagram_engine: every edit is one Ops call inside
// DrawingDocument::edit (one undo step), every read is a map with fixed
// key names. It owns the window's view state that commands act on — the
// current page, the selection (item ids on that page), the active tool, the
// format painter — and holds no copy of the drawing.
//
// AGENT-CONTRACT (with qml/*.qml and canvas_adapter.h / page_view.h):
//  * Coordinates are page units (1/96 in), y down; QML maps view <-> page
//    through the CanvasView (mapToDocument / mapFromDocument).
//  * `revision` bumps after every content change (edit, undo, redo, load);
//    views repaint on `changed`, and on `currentPageChanged`.
//  * The selection is always group-expanded and pruned to the current page.
//  * Refusals come back as `notify(title, text, "warning")`, never silently.
//
// Files: diagram_service.cpp (selection, tool state, item maps),
// diagram_service_pages.cpp (pages, page settings, canvas reads),
// diagram_service_edit.cpp (shapes, connectors, arrange, style, data),
// diagram_service_io.cpp (clipboard, find, import/export, layout); M87
// (D-095): diagram_service_review.cpp (layers, hyperlinks, comments,
// background pages, print setup), diagram_service_structure.cpp
// (containers, swimlanes, AutoConnect, Re-Layout), diagram_service_data.cpp
// (external data).
//
// AGENT-CONTRACT (M87): every page edit goes through editPage, which after
// the change lets Visio's container behaviour settle (Ops::settle) and Auto
// Size grow the page (Ops::applyAutoSize) — in the same undo step.

#include "drawing.h"
#include "drawing_document.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace QindaOffice::Apps::Diagram {

namespace Engine = ::QindaOffice::Diagram;

class DiagramService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DiagramHost owns the service.")
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId NOTIFY currentPageChanged)
    Q_PROPERTY(int currentPageIndex READ currentPageIndex NOTIFY currentPageChanged)
    // The current page: name, width, height, landscape, paper, sizeText,
    // grid, gridSpacing, lineJumps, background, shapeCount, connectorCount.
    Q_PROPERTY(QVariantMap page READ pageInfo NOTIFY pageInfoChanged)
    Q_PROPERTY(QStringList selection READ selection WRITE setSelection NOTIFY selectionChanged)
    // "none" | "shape" | "connector" | "mixed"
    Q_PROPERTY(QString selectionKind READ selectionKind NOTIFY selectionChanged)
    // The first selected item in full (itemMap keys), or {}.
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    // "pointer" | "connector" | "text"
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(bool formatPainter READ formatPainter WRITE setFormatPainter NOTIFY formatPainterChanged)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QVariantList themes READ themeList CONSTANT)
    // ShapePalette definitions: id, label, category, description, imageSource.
    Q_PROPERTY(QVariantList stencils READ stencilDefinitions NOTIFY stencilsChanged)
    Q_PROPERTY(QVariantList paperSizes READ paperSizes CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectedChanged)
    // The item whose text an in-place editor covers ("" when none).
    Q_PROPERTY(QString editingTextId READ editingTextId WRITE setEditingTextId NOTIFY editingTextIdChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    // ---- M87 (D-095) ----
    // The current page's layers: index, name, visible, print, locked,
    // active, glue, color, count (items on it).
    Q_PROPERTY(QVariantList layers READ layerList NOTIFY changed)
    // The current page's comment threads: id, author, initials, date,
    // text, done, itemId, anchor, replies [{id, author, date, text}].
    Q_PROPERTY(QVariantList comments READ commentList NOTIFY changed)
    Q_PROPERTY(int commentTotal READ commentTotal NOTIFY changed)
    Q_PROPERTY(QString activeComment READ activeComment WRITE setActiveComment NOTIFY activeCommentChanged)
    // Recordsets: id, name, source, sheet, columns, rowCount, keyColumn, refreshed, linked.
    Q_PROPERTY(QVariantList recordsets READ recordsetList NOTIFY changed)
    // Background pages of the drawing: id, name.
    Q_PROPERTY(QVariantList backgroundPages READ backgroundPageList NOTIFY changed)
    // The stencil AutoConnect adds ("" = like the shape it starts from).
    Q_PROPERTY(QString quickShape READ quickShape WRITE setQuickShape NOTIFY quickShapeChanged)
public:
    explicit DiagramService(Engine::DrawingDocument *document, QObject *parent = nullptr);

    // ---- C++ side (host, canvas adapter, page view) ----
    [[nodiscard]] Engine::DrawingDocument *document() const { return m_document; }
    [[nodiscard]] const Engine::Drawing &drawing() const;
    [[nodiscard]] const Engine::Page *currentPage() const;
    // One undo step changing the current page (the drawing is passed for
    // fresh ids). False when nothing changed.
    bool editPage(const QString &label, const std::function<bool(Engine::Drawing &, Engine::Page &)> &change,
                  const QString &mergeKey = {});
    // Moves/resizes/rotates from the canvas (one step for the whole gesture).
    struct Geometry {
        QString id;
        QRectF rect;
        double rotation = 0;
    };
    bool applyGeometry(const QVector<Geometry> &shapes, const QStringList &movedConnectors, QPointF connectorDelta,
                       const QStringList &removed, const QString &label);

    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QVariantList pages() const;
    [[nodiscard]] QString currentPageId() const { return m_pageId; }
    void setCurrentPageId(const QString &id);
    [[nodiscard]] int currentPageIndex() const;
    [[nodiscard]] QVariantMap pageInfo() const;
    [[nodiscard]] QStringList selection() const { return m_selection; }
    void setSelection(const QStringList &ids);
    [[nodiscard]] QString selectionKind() const;
    [[nodiscard]] QVariantMap selected() const;
    [[nodiscard]] QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    [[nodiscard]] bool formatPainter() const { return m_formatPainter; }
    void setFormatPainter(bool armed);
    [[nodiscard]] QString themeId() const;
    [[nodiscard]] QVariantList themeList() const;
    [[nodiscard]] QVariantList stencilDefinitions() const;
    [[nodiscard]] QVariantList paperSizes() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString editingTextId() const { return m_editingTextId; }
    void setEditingTextId(const QString &id);
    [[nodiscard]] bool canPaste() const;

    // ---- pages (diagram_service.cpp) ----
    Q_INVOKABLE QString addPage();
    Q_INVOKABLE QString duplicatePage();
    Q_INVOKABLE bool deletePage(const QString &id = {});
    Q_INVOKABLE bool renamePage(const QString &id, const QString &name);
    Q_INVOKABLE bool movePage(const QString &id, int delta);
    Q_INVOKABLE bool setPaper(const QString &paper);
    Q_INVOKABLE bool setLandscape(bool landscape);
    Q_INVOKABLE bool setPageSize(double width, double height);
    Q_INVOKABLE bool setGridVisible(bool visible);
    Q_INVOKABLE bool setLineJumps(bool on);
    Q_INVOKABLE bool setPageBackground(const QString &color);
    Q_INVOKABLE bool fitPageToContents();
    Q_INVOKABLE bool applyTheme(const QString &themeId);

    // ---- reads for the canvas QML ----
    Q_INVOKABLE QVariantMap item(const QString &id) const;
    Q_INVOKABLE QString shapeAt(double x, double y) const;
    // {shapeId, port, x, y}: where a connector end dropped at (x, y) glues.
    Q_INVOKABLE QVariantMap glueAt(double x, double y, double tolerance, const QString &ignore = {}) const;
    // Page-space points of a shape's connection points.
    Q_INVOKABLE QVariantList ports(const QString &shapeId) const;
    // Page-space box of an item's text (the in-place editor's rectangle).
    Q_INVOKABLE QRectF textBox(const QString &id) const;
    Q_INVOKABLE QRectF selectionBounds() const;
    Q_INVOKABLE QRectF contentBounds() const;
    // Route points of a connector (page space).
    Q_INVOKABLE QVariantList route(const QString &connectorId) const;
    Q_INVOKABLE void selectAll();

    // ---- shapes and connectors (diagram_service_edit.cpp) ----
    Q_INVOKABLE QString addShape(const QString &stencilId, double x, double y);
    Q_INVOKABLE QString connectItems(const QVariantMap &from, const QVariantMap &to);
    Q_INVOKABLE bool setConnectorEnd(const QString &connectorId, bool start, double x, double y);
    Q_INVOKABLE bool setText(const QString &id, const QString &text);
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE QStringList duplicateSelection();
    Q_INVOKABLE bool nudgeSelection(double dx, double dy);
    Q_INVOKABLE bool align(const QString &edge);
    Q_INVOKABLE bool distribute(const QString &axis);
    Q_INVOKABLE bool order(const QString &how);
    Q_INVOKABLE bool group();
    Q_INVOKABLE bool ungroup();
    Q_INVOKABLE bool rotateSelection(double degrees);
    Q_INVOKABLE bool flipSelection(const QString &axis);
    Q_INVOKABLE bool setAspectLocked(bool locked);
    // style keys: fill, gradient, line, lineWidth, pattern, corner, shadow, opacity
    Q_INVOKABLE bool setStyle(const QVariantMap &style);
    // text keys: family, size, bold, italic, underline, color, align (left|center|right),
    // valign (top|middle|bottom); `grow` (+1/-1) steps the size
    Q_INVOKABLE bool setTextStyle(const QVariantMap &text);
    // connector keys: routing, startArrow, endArrow, labelPosition
    Q_INVOKABLE bool setConnectorStyle(const QVariantMap &style);
    Q_INVOKABLE bool quickStyle(int variant);
    Q_INVOKABLE bool applyFormatTo(const QStringList &ids);
    Q_INVOKABLE bool setData(const QString &id, const QString &key, const QString &value);
    Q_INVOKABLE bool removeData(const QString &id, const QString &key);

    // ---- M87 (D-095): layers, links, comments, background pages, print
    // setup (diagram_service_review.cpp) ----
    [[nodiscard]] QVariantList layerList() const;
    Q_INVOKABLE int addLayer(const QString &name);
    Q_INVOKABLE bool removeLayer(int index);
    // key: name | visible | print | locked | active | glue | color ("" = none)
    Q_INVOKABLE bool setLayerProperty(int index, const QString &key, const QVariant &value);
    // The layers the selection is on (indexes, the first item's).
    Q_INVOKABLE QVariantList selectionLayers() const;
    Q_INVOKABLE bool assignSelectionToLayers(const QVariantList &indexes);
    // [{description, address, subAddress}] of an item.
    Q_INVOKABLE QVariantList links(const QString &itemId) const;
    Q_INVOKABLE bool setSelectionLinks(const QVariantList &links);
    // Follows an item's first link: a page of the drawing, or the address
    // through the desktop (never in tests: they follow page links only).
    Q_INVOKABLE bool followLink(const QString &itemId);
    [[nodiscard]] QVariantList commentList() const;
    [[nodiscard]] int commentTotal() const;
    [[nodiscard]] QString activeComment() const { return m_activeComment; }
    void setActiveComment(const QString &id);
    // On the selection's first item, else on the page; a reply to `parentId`.
    Q_INVOKABLE QString addComment(const QString &text, const QString &parentId = {});
    Q_INVOKABLE bool editComment(const QString &id, const QString &text);
    Q_INVOKABLE bool setCommentDone(const QString &id, bool done);
    Q_INVOKABLE bool deleteComment(const QString &id = {});
    // Walks every comment thread of the drawing (+1 / -1), page by page.
    Q_INVOKABLE QString stepComment(int step);
    // Screen markers: [{id, x, y}] in page units (one per commented item and
    // for the page's own comments).
    Q_INVOKABLE QVariantList commentMarkers() const;
    [[nodiscard]] QVariantList backgroundPageList() const;
    Q_INVOKABLE bool setBackgroundPage(bool background);
    Q_INVOKABLE bool setBackPage(const QString &backId);
    Q_INVOKABLE QString addBackgroundPage();
    Q_INVOKABLE bool addBorderAndTitle();
    Q_INVOKABLE bool setAutoSize(bool on);
    // style: arc | gap | square; on: lastRouted | horizontal | vertical
    Q_INVOKABLE bool setLineJumpStyle(const QString &style, const QString &on);
    // {paper ("" = the printer's, else a paper name or "page"), landscape,
    // zoom (0 = fit), across, down, center}
    Q_INVOKABLE bool setPrintSetup(const QVariantMap &setup);
    // Where the printed sheets meet on the current page (rects, page units).
    Q_INVOKABLE QVariantList pageBreaks() const;

    // ---- M87: containers, swimlanes, AutoConnect, Re-Layout
    // (diagram_service_structure.cpp) ----
    // A lane before/after the selected one, else joining the nearest pool.
    Q_INVOKABLE QString insertSwimlane(const QString &type, bool before, double x, double y);
    Q_INVOKABLE QString insertPhase(double x, double y);
    // A container of `type` around the selection, else one at (x, y).
    Q_INVOKABLE QString insertContainer(const QString &type, double x, double y);
    Q_INVOKABLE bool fitContainers();
    Q_INVOKABLE bool disbandContainers();
    // resize: expand | fit | none
    Q_INVOKABLE bool setContainerResize(const QString &resize);
    Q_INVOKABLE bool setContainerMargin(double margin);
    Q_INVOKABLE bool selectContainerContents();
    // The selected container's behaviour: resize, margin ({} for none).
    Q_INVOKABLE QVariantMap containerInfo() const;
    [[nodiscard]] QString quickShape() const { return m_quickShape; }
    void setQuickShape(const QString &stencilId);
    // side: 0 top, 1 right, 2 bottom, 3 left (Visio's AutoConnect arrows).
    Q_INVOKABLE QString autoConnect(const QString &fromId, int side);
    // {kind, rankGap, nodeGap}; kinds as LayoutKind names (layout.h).
    Q_INVOKABLE bool relayout(const QVariantMap &options);

    // ---- M87: external data (diagram_service_data.cpp) ----
    // A preview of a file: columns, rows (the first 50), sheets, sheet; or
    // {error}.
    Q_INVOKABLE QVariantMap readDataFile(const QUrl &file, const QString &sheet = {});
    // Links the file as a recordset; matches shapes when `matchColumn` ≥ 0
    // (their text, or data field `shapeField`). Returns the recordset id.
    Q_INVOKABLE QString linkData(const QUrl &file, const QString &sheet, int keyColumn, int matchColumn,
                                 const QString &shapeField);
    [[nodiscard]] QVariantList recordsetList() const;
    // Rows of a recordset with how many shapes link to each: [{row, cells, linked}].
    Q_INVOKABLE QVariantList recordsetRows(const QString &recordsetId) const;
    Q_INVOKABLE bool linkSelectionToRow(const QString &recordsetId, int row);
    Q_INVOKABLE int autoLinkData(const QString &recordsetId, int column, const QString &shapeField);
    Q_INVOKABLE bool unlinkSelection();
    Q_INVOKABLE bool refreshData(const QString &recordsetId = {});
    Q_INVOKABLE bool removeRecordset(const QString &recordsetId);
    Q_INVOKABLE QStringList shapesFromRows(const QString &recordsetId, const QVariantList &rows,
                                           const QString &stencilId, int textColumn);

    // ---- clipboard, find, import/export, layout (diagram_service_io.cpp) ----
    Q_INVOKABLE bool copy();
    Q_INVOKABLE bool cut();
    Q_INVOKABLE QStringList paste();
    Q_INVOKABLE bool copyAsImage();
    Q_INVOKABLE int find(const QString &text);
    Q_INVOKABLE int findNext(int step);
    Q_INVOKABLE int replace(const QString &find, const QString &replacement);
    // kind: "topDown" | "leftRight" | "tree"
    Q_INVOKABLE bool autoLayout(const QString &kind);
    Q_INVOKABLE bool importDrawio(const QUrl &file);
    Q_INVOKABLE bool importOrgChart(const QUrl &file);
    Q_INVOKABLE bool importOrgChartText(const QString &csv);
    Q_INVOKABLE bool insertPicture(const QUrl &file, double x, double y);
    Q_INVOKABLE bool exportPng(const QUrl &file, bool selectionOnly);
    Q_INVOKABLE bool exportSvg(const QUrl &file, bool selectionOnly);

signals:
    void changed();
    void pageInfoChanged();
    void currentPageChanged();
    void selectionChanged();
    void selectedChanged();
    void toolChanged();
    void formatPainterChanged();
    void stencilsChanged();
    void editingTextIdChanged();
    void clipboardChanged();
    // variant: "info" | "success" | "warning"
    void notify(const QString &title, const QString &text, const QString &variant);
    // The view should bring this page-space rectangle into sight.
    void revealRequested(const QRectF &rect);
    void activeCommentChanged();
    void quickShapeChanged();
    // Begin in-place text editing of the item.
    void textEditRequested(const QString &id);

private:
    void documentChanged();
    void pruneSelection();
    [[nodiscard]] QVariantMap itemMap(const QString &id) const;
    void refuse(const QString &title, const QString &text);
    void selectAndReveal(const QStringList &ids);

    Engine::DrawingDocument *m_document;
    QString m_pageId;
    QStringList m_selection;
    QString m_tool = QStringLiteral("pointer");
    bool m_formatPainter = false;
    QString m_formatSource;
    QString m_editingTextId;
    QStringList m_findMatches;
    int m_findIndex = -1;
    int m_revision = 0;
    int m_pasteCount = 0;
    QString m_activeComment;   // M87
    QString m_quickShape;      // M87
    mutable QString m_stencilTheme;
    mutable QVariantList m_stencilCache;
};

} // namespace QindaOffice::Apps::Diagram
