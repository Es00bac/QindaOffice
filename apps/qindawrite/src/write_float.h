// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Wrap Text and Position for the selected picture (M77, D-086;
// docx_float.h): In Line with Text, Square, Tight, Through, Top and
// Bottom, Behind Text, In Front of Text; Move with Text / Fix Position on
// Page; More Layout Options (the Layout dialog: horizontal and vertical
// position — alignment or offset — relative to the margin, page, column,
// paragraph or line, the wrap side and the distances from the text).
// Each command is one undo step; switching from In Line keeps the picture
// where it is, as Word does. The commands are enabled while a picture is
// selected and their check marks follow it.

#include "action_model.h"

#include <QObject>
#include <QVariantMap>

#include <functional>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class PictureTool;
class WriteDocument;

class PictureLayoutController final : public QObject {
    Q_OBJECT
public:
    PictureLayoutController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                            QObject *parent);

    // The page item's picture tool, once (the selection lives there).
    void setPictureTool(PictureTool *tool) { m_tool = tool; }
    // Format ▸ Wrap Text ▸ …, Position…
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // "inline", "square", "tight", "through", "topAndBottom", "behind",
    // "inFront" on the selected picture; one undo step.
    bool applyWrap(const QString &wrap);
    bool setMoveWithText(bool move);

    // The Layout dialog: {wrap, wrapText, hRelative, hAlign, hOffset,
    // vRelative, vAlign, vOffset (px), distTop, distBottom, distLeft,
    // distRight, moveWithText, allowOverlap}; empty without a picture.
    Q_INVOKABLE QVariantMap layoutInfo() const;
    Q_INVOKABLE bool applyLayout(const QVariantMap &answer);
    // Re-reads the selection (check marks, enabled state).
    void sync();

signals:
    void layoutDialogRequested();

private:
    bool apply(const std::function<bool(int)> &change);

    PageTextControl *m_control;
    WriteDocument *m_document;
    PictureTool *m_tool = nullptr;
    QList<Quick::ActionEntry *> m_wraps;
    Quick::ActionEntry *m_moveWithText = nullptr;
    Quick::ActionEntry *m_fixPosition = nullptr;
    Quick::ActionEntry *m_layout = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
