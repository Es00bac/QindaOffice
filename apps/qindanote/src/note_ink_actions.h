// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote's Ink commands (M22, plan section 3.7): the four tools, the
// pen/highlighter colour and width pickers and the eraser options as ONE
// set of persistent window-scoped QActions shared by the Ink menu and the
// tool strip's popup buttons (AGENTS.md rule 5 — the strip never clones a
// menu action; two actions answering to one objectName is the identity
// collision the rule forbids).
//
// Per-tool state (last tool, colours, widths, eraser mode/size) is
// window-independent STATE, not a preference (D-026): tools.json in the
// app's state dir through Core::JsonSettings, written on change, read at
// window construction (rule 7: bounded, per app, degrades by falling back
// to the defaults).
//
// AGENT-NOTE: ink colours are DOCUMENT data — they persist into notebook
// files — not window chrome, so the colour literals and the painted
// swatch icons in the .cpp are allowed (AGENTS.md rule 3 covers chrome).

#include "stroke_edit.h"

#include <QColor>
#include <QObject>

#include <memory>

class QAction;
class QActionGroup;
class QMenu;
class QToolButton;

namespace QindaOffice::Core {
class JsonSettings;
}

namespace QindaOffice::Shell {
class WrappingToolBar;
}

namespace QindaOffice::Apps::Notes {

class NotePageView;
class NoteWindow;

class NoteInkActions final : public QObject {
    Q_OBJECT
public:
    NoteInkActions(NoteWindow *window, NotePageView *view);
    // Out of line: m_state is a unique_ptr to a forward-declared type.
    ~NoteInkActions() override;

    // The window's &Ink menu (noteInkMenu) and the top tool strip.
    void populateInkMenu(QMenu *menu);
    void addToStrip(Shell::WrappingToolBar *strip);

private:
    void createToolActions();
    void createPickerActions();
    // Every picker change routes through an apply* (view + tools.json);
    // checked states and button icons follow through presentationChanged.
    void applyPenColor(const QColor &color);
    void applyPenWidth(qreal width);
    void applyHighlighterColor(const QColor &color);
    void applyHighlighterWidth(qreal width);
    void applyEraserMode(Ink::EraseMode mode);
    void applyEraserRadius(qreal radius);
    void syncFromView();
    // The two strip popup buttons track the ACTIVE tool: Pen shows the pen
    // pickers, Highlighter the highlighter pickers, Eraser the size menu
    // (colour disabled), Text neither.
    void syncPopupButtons();
    void loadState();
    void saveState();
    [[nodiscard]] QMenu *penColorMenu(QWidget *parent) const;
    [[nodiscard]] QMenu *penWidthMenu(QWidget *parent) const;
    [[nodiscard]] QMenu *highlighterColorMenu(QWidget *parent) const;
    [[nodiscard]] QMenu *highlighterWidthMenu(QWidget *parent) const;
    [[nodiscard]] QMenu *eraserMenu(QWidget *parent, bool withModes) const;

    NoteWindow *m_window;
    NotePageView *m_view;
    QActionGroup *m_toolGroup = nullptr;
    QAction *m_penTool = nullptr;
    QAction *m_highlighterTool = nullptr;
    QAction *m_eraserTool = nullptr;
    QAction *m_textTool = nullptr;
    QActionGroup *m_penColorGroup = nullptr;
    QList<QAction *> m_penColors;
    QAction *m_customColor = nullptr;
    QActionGroup *m_penWidthGroup = nullptr;
    QList<QAction *> m_penWidths;
    QActionGroup *m_highlighterColorGroup = nullptr;
    QList<QAction *> m_highlighterColors;
    QActionGroup *m_highlighterWidthGroup = nullptr;
    QList<QAction *> m_highlighterWidths;
    QActionGroup *m_eraserModeGroup = nullptr;
    QList<QAction *> m_eraserModes;
    QActionGroup *m_eraserSizeGroup = nullptr;
    QList<QAction *> m_eraserSizes;
    QToolButton *m_colorButton = nullptr;
    QToolButton *m_widthButton = nullptr;
    // The popup buttons' persistent menus (created once in addToStrip; a
    // QMenu has one parent, so the Ink menu gets its own instances of the
    // same shared actions).
    QMenu *m_btnPenColor = nullptr;
    QMenu *m_btnPenWidth = nullptr;
    QMenu *m_btnHlColor = nullptr;
    QMenu *m_btnHlWidth = nullptr;
    QMenu *m_btnEraser = nullptr;
    std::unique_ptr<Core::JsonSettings> m_state;
};

} // namespace QindaOffice::Apps::Notes
