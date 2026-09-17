// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaQt Office launcher window: four app cards and the suite's recent
// documents.
//
// It is a front door, not a document window, so the one-document-per-window
// rule does not apply to it — but everything else does: ordinary top-level
// window, desktop identity pinned before it maps, stock widgets, palette
// colours only. It starts the apps DETACHED and never edits their state.

#include "launcher_catalog.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;

namespace QindaOffice::Launcher {

class LauncherWindow final : public QWidget {
    Q_OBJECT
public:
    explicit LauncherWindow(QWidget *parent = nullptr);

    // Test seam: the recent list as the window built it.
    [[nodiscard]] QListWidget *recentList() const { return m_recent; }

private:
    void buildCards(QGridLayout *grid);
    void refreshRecents();
    void openApp(const AppEntry &app, const QString &path = {});
    void openRecent(int row);
    void report(const QString &text);

    QListWidget *m_recent = nullptr;
    QLabel *m_status = nullptr;
    QList<RecentEntry> m_recentEntries;
};

} // namespace QindaOffice::Launcher
