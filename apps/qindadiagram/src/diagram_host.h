// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DiagramHost: one QindaDiagram window = one .qdiagram drawing (M50,
// D-050; rule 1). The host owns the DrawingDocument (undo through its
// stack), the window's DiagramService (`diagram`), the commands and menus,
// the view toggles the window binds to, and the ONE print callback Print,
// Print Preview and Export PDF render (diagram_engine's pagesPainter).
//
// AGENT-CONTRACT (with qml/DiagramWindow.qml and friends):
//  * Commands are ActionEntries with the app-scoped ids of
//    diagram_host_actions.cpp (rule 5 — never rename one). A command either
//    calls the service or emits one of the request signals below, which the
//    QML answers (colour pickers, file dialogs, prompts, zoom).
//  * `viewCenter` is written by the canvas (page units): commands that
//    insert a shape put it there.
//  * Ribbon contexts: "shape" while shapes are selected, "connector" while
//    connectors are.
//
// Files: diagram_host.cpp (document, view state, printing),
// diagram_host_actions.cpp (commands), diagram_host_menus.cpp (menus and
// enablement).

#include "diagram_service.h"
#include "document_host.h"

#include <QPointF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Diagram {
class DrawingDocument;
}

namespace QindaOffice::Apps::Diagram {

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class DiagramHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QindaOffice::Apps::Diagram::DiagramService *diagram READ diagram CONSTANT)
    Q_PROPERTY(QPointF viewCenter READ viewCenter WRITE setViewCenter NOTIFY viewCenterChanged)
    Q_PROPERTY(bool shapesPanelVisible READ shapesPanelVisible WRITE setShapesPanelVisible NOTIFY viewStateChanged)
    Q_PROPERTY(bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible NOTIFY viewStateChanged)
    Q_PROPERTY(bool minimapVisible READ minimapVisible WRITE setMinimapVisible NOTIFY viewStateChanged)
    Q_PROPERTY(bool rulersVisible READ rulersVisible WRITE setRulersVisible NOTIFY viewStateChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY viewStateChanged)
public:
    DiagramHost();
    ~DiagramHost() override;

    [[nodiscard]] DiagramService *diagram() const { return m_service; }
    [[nodiscard]] ::QindaOffice::Diagram::DrawingDocument *drawingDocument() const { return m_document; }

    [[nodiscard]] QPointF viewCenter() const { return m_viewCenter; }
    void setViewCenter(QPointF centre);
    [[nodiscard]] bool shapesPanelVisible() const;
    void setShapesPanelVisible(bool visible);
    [[nodiscard]] bool inspectorVisible() const;
    void setInspectorVisible(bool visible);
    [[nodiscard]] bool minimapVisible() const;
    void setMinimapVisible(bool visible);
    [[nodiscard]] bool rulersVisible() const;
    void setRulersVisible(bool visible);
    [[nodiscard]] bool snapEnabled() const;
    void setSnapEnabled(bool enabled);

    // Export PDF through the print callback (landscape when the first page is).
    Q_INVOKABLE bool exportPdfTo(const QUrl &file);

signals:
    void viewCenterChanged();
    void viewStateChanged();
    // what: "fill" | "line" | "text" | "background"
    void colorRequested(const QString &what);
    // what: "picture" | "orgChart" | "drawio"
    void openFileRequested(const QString &what);
    // what: "png" | "svg" | "pdf" | "pngSelection" | "svgSelection"
    void exportRequested(const QString &what);
    void pageSetupRequested();
    void renamePageRequested();
    void findRequested();
    void replaceRequested();
    // how: "in" | "out" | "reset" | "page" | "selection"
    void zoomRequested(const QString &how);
    // The inspector should show a section ("data", "style", "text", "page").
    void inspectorRequested(const QString &section);

protected:
    // QindaDiagram drawings and Visio 2013+ drawings and templates open
    // (M65, D-073); Visio 2003–2010 files are offered so they meet a clear
    // refusal. Save As writes .qdiagram, .vsdx or .vstx.
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // diagram_host_actions.cpp
    void createActions();
    void wireActions();
    // diagram_host_menus.cpp
    void insertMenus();
    void syncEnablement();
    void syncChecks();
    bool toggled(const char *id) const;

    ::QindaOffice::Diagram::DrawingDocument *m_document = nullptr;
    DiagramService *m_service = nullptr;
    QPointF m_viewCenter{528, 408};
};

} // namespace QindaOffice::Apps::Diagram
