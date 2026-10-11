// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_control.h"
#include "page_typing_format.h"

#include "docx_list_labels.h"

#include "docx_properties.h"
#include "docx_table_format.h"
#include "docx_text.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTextTable>
#include <QTimer>

namespace QindaOffice::Apps::Write {

namespace {

// AGENT-NOTE (D-004): a page is paper. These are the colours the print
// path and the PDF use, pinned here so no desktop theme can turn the page
// dark and leave imported blocks as white boxes.
QPalette makePaperPalette()
{
    QPalette paper;
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Window, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    paper.setColor(QPalette::WindowText, Qt::black);
    paper.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd6));
    paper.setColor(QPalette::HighlightedText, Qt::white);
    return paper;
}

} // namespace

PageTextControl::PageTextControl(QTextDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_cursor(document)
    , m_palette(makePaperPalette())
    , m_blink(new QTimer(this))
{
    m_charFormat = typingFormat(m_cursor);
    m_blink->setInterval(QGuiApplication::styleHints()->cursorFlashTime() / 2 > 0
                             ? QGuiApplication::styleHints()->cursorFlashTime() / 2
                             : 500);
    connect(m_blink, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        emit updateRequest();
    });
    bindDocument();
}

PageTextControl::~PageTextControl() = default;

void PageTextControl::bindDocument()
{
    connect(m_document, &QTextDocument::undoAvailable, this, &PageTextControl::undoAvailable);
    connect(m_document, &QTextDocument::redoAvailable, this, &PageTextControl::redoAvailable);
    connect(m_document, &QTextDocument::contentsChanged, this, [this] {
        // An edit made elsewhere (a controller, the assistant, a load)
        // repaints and re-reads the format under the caret.
        if (!m_formatPinned) {
            m_charFormat = typingFormat(m_cursor);
        }
        emit updateRequest();
    });
}

void PageTextControl::setDocument(QTextDocument *document)
{
    if (document == nullptr || document == m_document) {
        return;
    }
    disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    m_cursor = QTextCursor(document);
    m_formatPinned = false;
    m_preedit.clear();
    bindDocument();
    emit undoAvailable(document->isUndoAvailable());
    emit redoAvailable(document->isRedoAvailable());
    emit documentChanged();
    cursorSettled(false);
}

// ---- cursor and format ------------------------------------------------------------

void PageTextControl::setTextCursor(const QTextCursor &cursor)
{
    if (cursor.document() != m_document) {
        return;
    }
    m_cursor = cursor;
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::setCurrentCharFormat(const QTextCharFormat &format)
{
    m_charFormat = format;
    m_formatPinned = true;
    emit currentCharFormatChanged(m_charFormat);
}

void PageTextControl::cursorSettled(bool ensureVisible)
{
    if (!m_formatPinned) {
        m_charFormat = typingFormat(m_cursor);
    }
    const bool selected = m_cursor.hasSelection();
    if (selected != m_hadSelection) {
        m_hadSelection = selected;
        emit copyAvailable(selected);
    }
    restartBlink();
    emit cursorPositionChanged();
    emit selectionChanged();
    emit currentCharFormatChanged(m_charFormat);
    emit microFocusChanged();
    if (ensureVisible) {
        emit visibilityRequest(cursorRect());
    }
    emit updateRequest();
}

void PageTextControl::restartBlink()
{
    m_cursorOn = true;
    if (m_focus && m_blink->interval() > 0) {
        m_blink->start();
    }
}

void PageTextControl::setFocus(bool focus)
{
    if (m_focus == focus) {
        return;
    }
    m_focus = focus;
    if (focus) {
        restartBlink();
    } else {
        m_blink->stop();
        m_cursorOn = false;
    }
    emit updateRequest();
}

QTextCursor PageTextControl::cursorForPosition(const QPointF &docPos) const
{
    const int position = m_document->documentLayout()->hitTest(docPos, Qt::FuzzyHit);
    QTextCursor cursor(m_document);
    if (position >= 0) {
        cursor.setPosition(position);
    }
    return cursor;
}

QRectF PageTextControl::cursorRect(const QTextCursor &cursor) const
{
    const QTextBlock block = cursor.block();
    if (!block.isValid() || block.layout() == nullptr) {
        return {};
    }
    const QRectF blockRect = m_document->documentLayout()->blockBoundingRect(block);
    const int relative = cursor.position() - block.position();
    const QTextLine line = block.layout()->lineForTextPosition(relative);
    if (!line.isValid()) {
        return {blockRect.topLeft(), QSizeF(1, blockRect.height())};
    }
    const qreal x = line.cursorToX(relative);
    return {blockRect.x() + x, blockRect.y() + line.y(), 1.0, line.height()};
}

QString PageTextControl::anchorAt(const QPointF &docPos) const
{
    return m_document->documentLayout()->anchorAt(docPos);
}

bool PageTextControl::find(const QString &needle, QTextDocument::FindFlags flags)
{
    const QTextCursor found = m_document->find(needle, m_cursor, flags);
    if (found.isNull()) {
        return false;
    }
    setTextCursor(found);
    return true;
}

// ---- events -----------------------------------------------------------------------

bool PageTextControl::processEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        return keyPress(static_cast<QKeyEvent *>(event));
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
        return mouse(static_cast<QMouseEvent *>(event));
    case QEvent::InputMethod:
        inputMethod(static_cast<QInputMethodEvent *>(event));
        return true;
    default:
        return false;
    }
}

bool PageTextControl::moveCursor(QKeyEvent *event)
{
    struct Binding {
        QKeySequence::StandardKey key;
        QTextCursor::MoveOperation op;
        QTextCursor::MoveMode mode;
    };
    static const Binding bindings[] = {
        {QKeySequence::MoveToNextChar, QTextCursor::Right, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToPreviousChar, QTextCursor::Left, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToNextWord, QTextCursor::NextWord, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToPreviousWord, QTextCursor::PreviousWord, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToNextLine, QTextCursor::Down, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToPreviousLine, QTextCursor::Up, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToStartOfLine, QTextCursor::StartOfLine, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToEndOfLine, QTextCursor::EndOfLine, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToStartOfBlock, QTextCursor::StartOfBlock, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToEndOfBlock, QTextCursor::EndOfBlock, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToStartOfDocument, QTextCursor::Start, QTextCursor::MoveAnchor},
        {QKeySequence::MoveToEndOfDocument, QTextCursor::End, QTextCursor::MoveAnchor},
        {QKeySequence::SelectNextChar, QTextCursor::Right, QTextCursor::KeepAnchor},
        {QKeySequence::SelectPreviousChar, QTextCursor::Left, QTextCursor::KeepAnchor},
        {QKeySequence::SelectNextWord, QTextCursor::NextWord, QTextCursor::KeepAnchor},
        {QKeySequence::SelectPreviousWord, QTextCursor::PreviousWord, QTextCursor::KeepAnchor},
        {QKeySequence::SelectNextLine, QTextCursor::Down, QTextCursor::KeepAnchor},
        {QKeySequence::SelectPreviousLine, QTextCursor::Up, QTextCursor::KeepAnchor},
        {QKeySequence::SelectStartOfLine, QTextCursor::StartOfLine, QTextCursor::KeepAnchor},
        {QKeySequence::SelectEndOfLine, QTextCursor::EndOfLine, QTextCursor::KeepAnchor},
        {QKeySequence::SelectStartOfBlock, QTextCursor::StartOfBlock, QTextCursor::KeepAnchor},
        {QKeySequence::SelectEndOfBlock, QTextCursor::EndOfBlock, QTextCursor::KeepAnchor},
        {QKeySequence::SelectStartOfDocument, QTextCursor::Start, QTextCursor::KeepAnchor},
        {QKeySequence::SelectEndOfDocument, QTextCursor::End, QTextCursor::KeepAnchor},
    };
    for (const Binding &binding : bindings) {
        if (event->matches(binding.key)) {
            m_cursor.movePosition(binding.op, binding.mode);
            m_formatPinned = false;
            cursorSettled(true);
            return true;
        }
    }
    return false;
}

void PageTextControl::deleteWithCursor(QTextCursor::MoveOperation operation)
{
    if (!m_cursor.hasSelection()) {
        m_cursor.movePosition(operation, QTextCursor::KeepAnchor);
    }
    m_cursor.removeSelectedText();
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::insertParagraph(bool lineBreak)
{
    if (lineBreak) {
        m_cursor.insertText(QString(QChar::LineSeparator), m_charFormat);
    } else {
        m_cursor.insertBlock();
    }
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::moveToNextCell(bool backward)
{
    QTextTable *table = m_cursor.currentTable();
    if (table == nullptr) {
        insertText(QStringLiteral("\t"));
        return;
    }
    const QTextTableCell cell = table->cellAt(m_cursor);
    int index = cell.row() * table->columns() + cell.column() + (backward ? -1 : 1);
    if (index < 0) {
        return;
    }
    if (index >= table->rows() * table->columns()) {
        // Tab in the last cell adds a row like the one above it (Word);
        // Qt's appendRows gives the new cells no format at all.
        m_cursor.beginEditBlock();
        table->appendRows(1);
        for (int column = 0; column < table->columns(); ++column) {
            Docx::copyNeighbourFormat(table, table->rows() - 1, column, table->rows() - 2, column);
        }
        m_cursor.endEditBlock();
        index = (table->rows() - 1) * table->columns();
    }
    const QTextTableCell next = table->cellAt(index / table->columns(), index % table->columns());
    setTextCursor(next.firstCursorPosition());
}

bool PageTextControl::keyPress(QKeyEvent *event)
{
    if (m_keyFilter && m_keyFilter(event)) {
        m_formatPinned = false;
        cursorSettled(true);
        return true;
    }
    if (moveCursor(event)) {
        return true;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
    } else if (event->matches(QKeySequence::Copy)) {
        copy();
    } else if (event->matches(QKeySequence::Cut)) {
        cut();
    } else if (event->matches(QKeySequence::Paste)) {
        paste();
    } else if (event->matches(QKeySequence::Undo)) {
        undo();
    } else if (event->matches(QKeySequence::Redo)) {
        redo();
    } else if (event->matches(QKeySequence::Delete)) {
        deleteWithCursor(QTextCursor::Right);
    } else if (event->matches(QKeySequence::Backspace)
               || (event->key() == Qt::Key_Backspace && (event->modifiers() & ~Qt::ShiftModifier) == 0)) {
        deleteWithCursor(QTextCursor::Left);
    } else if (event->matches(QKeySequence::DeleteEndOfWord)) {
        deleteWithCursor(QTextCursor::EndOfWord);
    } else if (event->matches(QKeySequence::DeleteStartOfWord)) {
        deleteWithCursor(QTextCursor::StartOfWord);
    } else if (event->matches(QKeySequence::DeleteEndOfLine)) {
        deleteWithCursor(QTextCursor::EndOfLine);
    } else if (event->matches(QKeySequence::InsertParagraphSeparator)) {
        insertParagraph(false);
    } else if (event->matches(QKeySequence::InsertLineSeparator)) {
        insertParagraph(true);
    } else if (event->key() == Qt::Key_Tab) {
        moveToNextCell(false);
    } else if (event->key() == Qt::Key_Backtab) {
        moveToNextCell(true);
    } else {
        const QString text = event->text();
        const bool printable = !text.isEmpty() && (text.at(0).isPrint() || text.at(0) == QLatin1Char('\t'))
            && (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0;
        if (!printable) {
            return false;
        }
        insertText(text);
    }
    return true;
}

bool PageTextControl::mouse(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (event->type() == QEvent::MouseButtonPress) {
        if (event->button() != Qt::LeftButton) {
            return false;
        }
        // Click counting: Qt's double-click event arrives as its own type;
        // a third press within the interval selects the paragraph.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool chained = now - m_lastPressTime < QGuiApplication::styleHints()->mouseDoubleClickInterval()
            && (pos - m_lastPress).manhattanLength() < 6;
        m_clicks = chained ? m_clicks + 1 : 1;
        m_lastPressTime = now;
        m_lastPress = pos;
        if (m_clicks >= 3) {
            m_cursor = cursorForPosition(pos);
            m_cursor.select(QTextCursor::BlockUnderCursor);
            m_clicks = 0;
        } else {
            const QTextCursor at = cursorForPosition(pos);
            if (event->modifiers() & Qt::ShiftModifier) {
                m_cursor.setPosition(at.position(), QTextCursor::KeepAnchor);
            } else {
                m_cursor = at;
            }
        }
        m_dragging = true;
        m_formatPinned = false;
        cursorSettled(false);
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (event->button() != Qt::LeftButton) {
            return false;
        }
        // AGENT-GUARD: WordUnderCursor groups adjacent object-replacement
        // characters as one word. ExactHit returns the painted object's
        // character even on its right half; FuzzyHit returns the next caret.
        const int exact = m_document->documentLayout()->hitTest(pos, Qt::ExactHit);
        if (exact >= 0 && exact < m_document->characterCount() - 1) {
            QTextCursor object(m_document); object.setPosition(exact);
            object.setPosition(exact + 1, QTextCursor::KeepAnchor);
            if (object.selectedText() == QString(QChar::ObjectReplacementCharacter)) {
                m_cursor = object;
                m_dragging = false;
                m_formatPinned = false;
                cursorSettled(false);
                emit doubleClicked(exact);
                return true;
            }
        }
        const int hit = cursorForPosition(pos).position();
        m_cursor = cursorForPosition(pos);
        m_cursor.select(QTextCursor::WordUnderCursor);
        m_dragging = false;
        m_formatPinned = false;
        cursorSettled(false);
        emit doubleClicked(hit);
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
            return false;
        }
        const QTextCursor at = cursorForPosition(pos);
        if (at.position() != m_cursor.position()) {
            m_cursor.setPosition(at.position(), QTextCursor::KeepAnchor);
            cursorSettled(true);
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        m_dragging = false;
        return event->button() == Qt::LeftButton;
    }
    return false;
}

void PageTextControl::inputMethod(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        QTextCursor cursor = m_cursor;
        cursor.setPosition(cursor.position() + event->replacementStart());
        cursor.setPosition(cursor.position() + event->replacementLength(), QTextCursor::KeepAnchor);
        cursor.insertText(event->commitString(), m_charFormat);
        m_cursor.setPosition(cursor.position());
    }
    // Preedit lives on the block's layout, never in the document.
    QTextBlock block = m_cursor.block();
    m_preedit = event->preeditString();
    if (QTextLayout *layout = block.layout(); layout != nullptr) {
        layout->setPreeditArea(m_cursor.positionInBlock(), m_preedit);
        m_document->markContentsDirty(block.position(), block.length());
    }
    cursorSettled(true);
}

QVariant PageTextControl::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const QTextBlock block = m_cursor.block();
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return cursorRect();
    case Qt::ImFont:
        return m_charFormat.font();
    case Qt::ImCursorPosition:
        return m_cursor.positionInBlock();
    case Qt::ImAnchorPosition:
        return m_cursor.anchor() - block.position();
    case Qt::ImSurroundingText:
        return block.text();
    case Qt::ImCurrentSelection:
        return m_cursor.selectedText();
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    default:
        return {};
    }
}

// ---- painting ---------------------------------------------------------------------

void PageTextControl::drawContents(QPainter *painter, const QRectF &clip) const
{
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = clip;
    context.palette = m_palette;
    context.cursorPosition = cursorOn() ? m_cursor.position() : -1;
    // A lone selected picture shows handles (page_picture_tool.h), not a
    // highlight tinting it, as in Word.
    const bool lonePicture = m_cursor.selectionEnd() - m_cursor.selectionStart() == 1
        && m_cursor.selectedText() == QString(QChar::ObjectReplacementCharacter) && m_cursor.charFormat().isImageFormat();
    if (m_cursor.hasSelection() && !lonePicture) {
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = m_cursor;
        selection.format.setBackground(m_palette.highlight());
        selection.format.setForeground(m_palette.highlightedText());
        context.selections.append(selection);
    }
    m_document->documentLayout()->draw(painter, context);
    Docx::drawListMarkers(painter, *m_document, clip); // Word's list numbers (M73), any story
}

} // namespace QindaOffice::Apps::Write
