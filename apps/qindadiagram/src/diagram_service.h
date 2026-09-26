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
// diagram_service_io.cpp (clipboard, find, import/export, layout).

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
    mutable QString m_stencilTheme;
    mutable QVariantList m_stencilCache;
};

} // namespace QindaOffice::Apps::Diagram
