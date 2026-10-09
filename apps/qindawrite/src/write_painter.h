// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Format Painter in QindaWrite (M73, D-082). The brush picks up the
// formatting at the selection — the character formatting of its first
// character, and the paragraph's formatting (its style, alignment,
// indents, spacing, borders, shading, tab stops) when the selection holds a
// whole paragraph or nothing — and lays it on the next text selected with
// the mouse (a click paints the word under it). One click on the button
// paints once; a second click within the double-click time locks the brush
// until Esc or another click on the button. Ctrl+Shift+C copies the
// formatting and Ctrl+Shift+V pastes it, as in Word. Each paint is one undo
// step; content, links, comments, fields, revisions and controls never
// travel with the formatting.

#include "action_model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTextBlockFormat>
#include <QTextCharFormat>

class QKeyEvent;
class QTextCursor;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class FormatPainter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
public:
    FormatPainter(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    // Home ▸ Format Painter, Copy Formatting, Paste Formatting.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] bool isLocked() const { return m_locked; }

    // Picks up the formatting at the control's selection.
    void copy();
    // Lays the picked-up formatting on the control's selection (the word
    // at the caret without one); false with nothing picked up.
    bool paste();
    // The button: arms the brush, locks it on a second click in time,
    // otherwise puts it away.
    void press();
    // The page's mouse selection ended (PageTextItem::selectionReleased).
    void selectionReleased();
    // Esc puts a brush away (the control's key filter).
    bool handleKey(QKeyEvent *event);

signals:
    void activeChanged();

private:
    void setActive(bool active, bool locked);

    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_brush = nullptr;
    Quick::ActionEntry *m_copy = nullptr;
    Quick::ActionEntry *m_paste = nullptr;
    QTextCharFormat m_char;
    QTextBlockFormat m_block;
    bool m_hasFormat = false;
    bool m_paragraph = false;
    bool m_active = false;
    bool m_locked = false;
    QElapsedTimer m_armed;
};

} // namespace QindaOffice::Apps::Write
