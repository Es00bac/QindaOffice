// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PageTextControl: the editing engine behind PageTextItem (conversion plan
// §5.1, D-029). The plan named QQuickTextControl; Qt builds it with
// Q_AUTOTEST_EXPORT, so the symbol is not in libQt6Quick and nothing
// outside Qt can link it. QWidgetTextControl is exported but needs a
// QtWidgets application. This class is the subset of that API QindaWrite uses,
// over the document alone: one cursor, a pending char format for the
// next keystroke, the standard key bindings, mouse selection (drag,
// double-click word, triple-click paragraph), the clipboard as HTML +
// text, input-method preedit, find, and a paint call that draws text,
// selection and caret in DOCUMENT coordinates with the paper palette
// pinned (D-004). Every method that takes or returns a point speaks
// document coordinates; the item maps from pages.

#include <QObject>
#include <QPalette>
#include <QPointF>
#include <QRectF>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <functional>

class QEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;

namespace QindaOffice::Apps::Write {

class PageTextControl final : public QObject {
    Q_OBJECT
public:
    explicit PageTextControl(QTextDocument *document, QObject *parent = nullptr);
    ~PageTextControl() override;

    [[nodiscard]] QTextDocument *document() const { return m_document; }
    // Edits another story — a header or footer (page_story_editor.h) — or
    // the body again: a fresh cursor at its start, undo/redo availability
    // re-announced. Every controller that works through textCursor() then
    // formats that story.
    void setDocument(QTextDocument *document);
    [[nodiscard]] QTextCursor textCursor() const { return m_cursor; }
    void setTextCursor(const QTextCursor &cursor);
    // The format the next typed character takes: the cursor's own, or
    // what setCurrentCharFormat() pinned (Bold with nothing selected).
    [[nodiscard]] QTextCharFormat currentCharFormat() const { return m_charFormat; }
    void setCurrentCharFormat(const QTextCharFormat &format);

    [[nodiscard]] QTextCursor cursorForPosition(const QPointF &docPos) const;
    [[nodiscard]] QRectF cursorRect(const QTextCursor &cursor) const;
    [[nodiscard]] QRectF cursorRect() const { return cursorRect(m_cursor); }
    [[nodiscard]] QString anchorAt(const QPointF &docPos) const;
    bool find(const QString &needle, QTextDocument::FindFlags flags = {});

    [[nodiscard]] bool hasFocus() const { return m_focus; }
    void setFocus(bool focus);
    [[nodiscard]] bool cursorOn() const { return m_focus && m_cursorOn; }
    [[nodiscard]] const QPalette &paperPalette() const { return m_palette; }

    // Key, mouse and input-method events; mouse positions are DOCUMENT
    // coordinates (the item maps them first). Returns true when consumed.
    bool processEvent(QEvent *event);
    // Keys that are a command of the host's before they are text: Tab and
    // Enter on list items, Esc for the Format Painter, Enter's "style for
    // following paragraph" (M73). The filter sees every key press first and
    // returns true when it handled it (as one edit of its own).
    void setKeyFilter(std::function<bool(QKeyEvent *)> filter) { m_keyFilter = std::move(filter); }
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const;
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] QString preeditText() const { return m_preedit; }

    // Text, selection and caret for `clip` (document coordinates).
    void drawContents(QPainter *painter, const QRectF &clip) const;

public slots:
    void cut();
    void copy();
    void paste();
    void undo();
    void redo();
    void selectAll();
    void insertText(const QString &text);

signals:
    // setDocument() switched stories.
    void documentChanged();
    void updateRequest();
    // The caret moved somewhere the view may have to scroll to.
    void visibilityRequest(const QRectF &docRect);
    void cursorPositionChanged();
    void selectionChanged();
    void currentCharFormatChanged(const QTextCharFormat &format);
    void copyAvailable(bool available);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void microFocusChanged();
    // A double-click settled at `position` (after the word selection).
    void doubleClicked(int position);

private:
    void bindDocument();
    bool keyPress(QKeyEvent *event);
    bool mouse(QMouseEvent *event);
    void inputMethod(QInputMethodEvent *event);
    bool moveCursor(QKeyEvent *event);
    void deleteWithCursor(QTextCursor::MoveOperation operation);
    void insertParagraph(bool lineBreak);
    void moveToNextCell(bool backward);
    void cursorSettled(bool ensureVisible);
    void restartBlink();

    QTextDocument *m_document;
    std::function<bool(QKeyEvent *)> m_keyFilter;
    QTextCursor m_cursor;
    QTextCharFormat m_charFormat;
    QPalette m_palette;
    QTimer *m_blink;
    QString m_preedit;
    QPointF m_lastPress;
    qint64 m_lastPressTime = 0;
    int m_clicks = 0;
    bool m_focus = false;
    bool m_cursorOn = true;
    bool m_dragging = false;
    bool m_formatPinned = false;
    bool m_hadSelection = false;
};

} // namespace QindaOffice::Apps::Write
