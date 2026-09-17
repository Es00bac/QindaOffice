// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow toolbox (M13): the shape-styling toolbar and the insert-shape
// command set as persistent named actions (action discipline, AGENTS.md
// rule 5), shared between the toolbar and the Insert/Format menus. Style
// actions mutate the model through the canvas so the old/new undo
// payloads keep flowing; the UI state re-reads the selected shape.

#include "presentation.h"

#include <QAction>
#include <QObject>

class QToolBar;

namespace QindaOffice::Apps::Show {

class SlideCanvas;

struct ShowStyleActions {
    QAction *bold = nullptr;
    QAction *italic = nullptr;
    QAction *fillColor = nullptr;
    QAction *strokeColor = nullptr;
    [[nodiscard]] QList<QAction *> styleActions() const;
};

struct ShowInsertActions {
    QAction *textBox = nullptr;
    QAction *rectangle = nullptr;
    QAction *ellipse = nullptr;
    QAction *line = nullptr;
    QAction *arrow = nullptr;
    QAction *image = nullptr;
    [[nodiscard]] QList<QAction *> all() const;
};

class ShowToolbox final : public QObject {
    Q_OBJECT
public:
    ShowToolbox(SlideCanvas *canvas, QObject *parent);

    [[nodiscard]] const ShowStyleActions &styleActions() const { return m_style; }
    [[nodiscard]] const ShowInsertActions &insertActions() const { return m_insert; }
    [[nodiscard]] QToolBar *toolBar();
    // Appends commands owned elsewhere (arrange/align) to the same wrapping
    // strip, so they are reachable without opening a menu.
    void appendStripActions(const QList<QAction *> &actions);

signals:
    // The image insert needs a file dialog at the window level; the
    // toolbox routes the request up instead of owning UI policy.
    void imageInsertRequested();

private:
    void pickColor(bool fill);
    void syncFromSelection();

    SlideCanvas *m_canvas;
    ShowStyleActions m_style;
    ShowInsertActions m_insert;
    QToolBar *m_toolBar = nullptr;
};

} // namespace QindaOffice::Apps::Show
