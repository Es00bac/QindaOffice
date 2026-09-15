// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite's persistent window-scoped format/insert actions (the action
// discipline in AGENTS.md: every command is a persistent QAction with a
// stable objectName from office_shell's ActionId table - the objectName is
// the dbusmenu activation identity). Menus and toolbars share these exact
// instances, so state (check marks, enablement) can never diverge between
// the menu bar and the formatting toolbar.

#include <QAction>
#include <QList>

class QObject;

namespace QindaOffice::Apps::Write {

struct WriteActions {
    QAction *bold = nullptr;
    QAction *italic = nullptr;
    QAction *underline = nullptr;
    QAction *strikeOut = nullptr;
    QAction *textColor = nullptr;
    QAction *clearFormat = nullptr;

    // Mutually exclusive paragraph alignment (one QActionGroup).
    QAction *alignLeft = nullptr;
    QAction *alignCenter = nullptr;
    QAction *alignRight = nullptr;
    QAction *alignJustify = nullptr;

    QAction *listBullet = nullptr;
    QAction *listNumbered = nullptr;
    QAction *indentMore = nullptr;
    QAction *indentLess = nullptr;

    QAction *insertTable = nullptr;
    QAction *insertImage = nullptr;
    QAction *insertColumns = nullptr;
    QAction *insertPageBreak = nullptr;

    [[nodiscard]] QList<QAction *> all() const;
};

// Builds the set parented to `parent` (the owning WriteWindow, so the
// Ctrl+B/I/U etc. shortcuts resolve window-scoped). Checkable state is set
// up here; behavior is connected by WriteWindow.
[[nodiscard]] WriteActions createWriteActions(QObject *parent);

} // namespace QindaOffice::Apps::Write
