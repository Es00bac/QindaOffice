// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "standard_actions.h"

#include <QMainWindow>

class QMenu;

namespace QindaOffice::Shell {

class GlobalMenuExport;

// One document = one ordinary top-level window (ADR-0110): no tab strip, no
// MDI, no document switcher. DocumentWindow is the suite's shared frame -
// standard menus/actions, per-window global-menu export, status bar - that
// the four apps host their document view inside via setCentralWidget() in
// subclasses (one per app, starting with M1).
//
// AGENT-GUARD (ADR-0117): never resize()/move()/setFixedSize() a shown main
// window; the desktop's window containers own grouped geometry. Only the
// initial pre-show size may be set here.
class DocumentWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DocumentWindow(QWidget *parent = nullptr);
    ~DocumentWindow() override;

    [[nodiscard]] QAction *action(const QString &objectName) const;
    [[nodiscard]] StandardActions *standardActions() { return &m_actions; }

signals:
    // Raised by file.new; OfficeApplication owns window lifetime and turns
    // this into a new ordinary window (never a tab of this one).
    void newWindowRequested();
    // Raised by file.open; M1 wires this to the file dialog + engine.
    void openRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    // App hook: subclasses insert their app-specific menus (Format/Insert/
    // Table/Data/Slide/Page) before the AI menu. Default inserts nothing.
    virtual void insertApplicationMenus(QMenu *before);

private:
    void createActions();
    void createMenus();
    void startMenuExport();
    void applyMenuBarVisibility(bool exported);

    StandardActions m_actions;
    GlobalMenuExport *m_menuExport = nullptr;
    bool m_menuExportStarted = false;
};

} // namespace QindaOffice::Shell
