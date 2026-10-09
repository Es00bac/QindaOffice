// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's share of the shell's edit commands (split from calc_host.cpp
// in M78 to keep both inside the line budget): Undo / Redo over the
// document's stack, Cut / Copy / Paste through the clipboard seam
// (calc_clipboard.h), Select All, and the zoom commands.

#include "calc_host.h"

#include "calc_clipboard.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUndoStack>

namespace QindaOffice::Apps::QindaCalc {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

// ---- the shell's edit commands ----------------------------------------------------

void CalcHost::wireEditCommands()
{
    ActionEntry *undo = action(QString::fromLatin1(ActionId::kEditUndo));
    ActionEntry *redo = action(QString::fromLatin1(ActionId::kEditRedo));
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(m_document->undoStack(), &QUndoStack::canUndoChanged, undo, &ActionEntry::setEnabled);
    connect(m_document->undoStack(), &QUndoStack::canRedoChanged, redo, &ActionEntry::setEnabled);
    connect(undo, &ActionEntry::triggered, this, [this] {
        m_document->undoStack()->undo();
        refreshGrid();
    });
    connect(redo, &ActionEntry::triggered, this, [this] {
        m_document->undoStack()->redo();
        refreshGrid();
    });

    action(QString::fromLatin1(ActionId::kEditCut))->setEnabled(true);
    action(QString::fromLatin1(ActionId::kEditCopy))->setEnabled(true);
    connect(action(QString::fromLatin1(ActionId::kEditCut)), &ActionEntry::triggered, this, [this] {
        cutRangeToClipboard(m_document, selection());
        refreshGrid();
    });
    connect(action(QString::fromLatin1(ActionId::kEditCopy)), &ActionEntry::triggered, this,
            [this] { copyRangeToClipboard(m_document, selection()); });
    ActionEntry *paste = action(QString::fromLatin1(ActionId::kEditPaste));
    const auto updatePaste = [paste] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        paste->setEnabled(mime != nullptr
                          && (mime->hasText()
                              || mime->hasFormat(QString::fromLatin1(richCellsMimeType()))));
    };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
    connect(paste, &ActionEntry::triggered, this, [this] {
        if (pasteClipboardAt(m_document, cursor())) {
            refreshGrid();
        }
    });
    action(QString::fromLatin1(ActionId::kEditSelectAll))->setEnabled(true);
    connect(action(QString::fromLatin1(ActionId::kEditSelectAll)), &ActionEntry::triggered, this,
            [this] {
                if (m_grid != nullptr) {
                    m_grid->selectUsedRange();
                }
            });
    for (const char *id : {ActionId::kViewZoomIn, ActionId::kViewZoomOut, ActionId::kViewZoomReset}) {
        action(QString::fromLatin1(id))->setEnabled(true);
    }
    connect(action(QString::fromLatin1(ActionId::kViewZoomIn)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(m_grid->zoom() * 1.25); });
    connect(action(QString::fromLatin1(ActionId::kViewZoomOut)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(m_grid->zoom() / 1.25); });
    connect(action(QString::fromLatin1(ActionId::kViewZoomReset)), &ActionEntry::triggered, this,
            [this] { if (m_grid != nullptr) m_grid->setZoom(1.0); });
}

} // namespace QindaOffice::Apps::QindaCalc
