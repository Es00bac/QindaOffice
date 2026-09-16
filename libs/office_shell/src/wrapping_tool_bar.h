// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A QToolBar whose contents WRAP onto further rows instead of disappearing
// behind the extension chevron.
//
// AGENT-CONTRACT: a toolbar shows its commands. Plain QToolBar hides
// whatever does not fit behind "»", so the formatting strip silently lost
// Align Left/Center/Right at ordinary window widths — buttons that exist,
// are enabled, and are never seen. Everything added through the addStrip*
// methods below is laid out by a FlowLayout and is always on screen.
//
// Buttons adapt per action: one whose QIcon::fromTheme() lookup resolved
// shows icon-only (compact, and the reason the strip fits at all); one whose
// icon is missing keeps its text, because an icon-only button with a null
// icon is a blank square. The full text is always the tooltip.

#include <QList>
#include <QToolBar>

class QAction;

namespace QindaOffice::Shell {

class FlowLayout;

class WrappingToolBar final : public QToolBar {
    Q_OBJECT
public:
    explicit WrappingToolBar(const QString &title, QWidget *parent = nullptr);

    // Adds a command button for `action` (the action stays the single source
    // of truth for enabled/checked/text, as the action discipline requires).
    void addStripAction(QAction *action);
    // Adds an arbitrary widget (font pickers, size combos) into the flow.
    void addStripWidget(QWidget *widget);
    void addStripSeparator();

    // The wrapping container, for tests and for measuring.
    [[nodiscard]] QWidget *strip() const { return m_strip; }
    // The commands on the strip, in order. QToolBar::actions() does NOT
    // report them: they live on the QToolButtons inside the flow, not in the
    // toolbar's own layout. This is the list to assert against.
    [[nodiscard]] QList<QAction *> stripActions() const { return m_stripActions; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Re-measures the wrapped height for the current width and applies it,
    // so the toolbar grows a row instead of clipping.
    void retuneHeight();

    QWidget *m_strip = nullptr;
    FlowLayout *m_flow = nullptr;
    QList<QAction *> m_stripActions;
};

} // namespace QindaOffice::Shell
