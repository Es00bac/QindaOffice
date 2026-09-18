// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite's persistent format/insert commands (AGENTS.md rule 5, on
// ActionEntry since M29): every command is one entry in the window's
// ActionModel with a stable id from office_quick's ActionId table — the
// id is the panel activation identity. Menus, the strips and the context
// menu share these exact entries, so a check mark cannot disagree.

#include "action_model.h"

#include <QList>

namespace QindaOffice::Apps::Write {

struct WriteActions {
    Quick::ActionEntry *bold = nullptr;
    Quick::ActionEntry *italic = nullptr;
    Quick::ActionEntry *underline = nullptr;
    Quick::ActionEntry *strikeOut = nullptr;
    Quick::ActionEntry *textColor = nullptr;
    Quick::ActionEntry *clearFormat = nullptr;

    // Mutually exclusive paragraph alignment (one radio group).
    Quick::ActionEntry *alignLeft = nullptr;
    Quick::ActionEntry *alignCenter = nullptr;
    Quick::ActionEntry *alignRight = nullptr;
    Quick::ActionEntry *alignJustify = nullptr;

    Quick::ActionEntry *listBullet = nullptr;
    Quick::ActionEntry *listNumbered = nullptr;
    Quick::ActionEntry *indentMore = nullptr;
    Quick::ActionEntry *indentLess = nullptr;

    Quick::ActionEntry *insertTable = nullptr;
    Quick::ActionEntry *insertImage = nullptr;
    Quick::ActionEntry *insertColumns = nullptr;
    Quick::ActionEntry *insertPageBreak = nullptr;

    [[nodiscard]] QList<Quick::ActionEntry *> all() const;
};

// Creates the entries in `model` (checkable state and the alignment radio
// group set up here; behaviour is connected by FormatController).
[[nodiscard]] WriteActions createWriteActions(Quick::ActionModel *model);

} // namespace QindaOffice::Apps::Write
