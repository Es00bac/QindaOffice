// SPDX-License-Identifier: GPL-3.0-or-later
// FormatPainter (write_painter.h): Word's Format Painter, Copy Formatting
// and Paste Formatting.

#include "write_painter.h"

#include "docx_format.h"
#include "docx_metrics.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"
#include "write_selection.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextList>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// A run's look — never its content, link, comment, field, revision or
// control (those are what the text is, not how it looks).
constexpr int kCharKeys[] = {QTextFormat::FontFamilies,     QTextFormat::FontFamily,         QTextFormat::FontPointSize,
                             QTextFormat::FontWeight,       QTextFormat::FontItalic,         QTextFormat::TextUnderlineStyle,
                             QTextFormat::TextUnderlineColor, QTextFormat::FontStrikeOut,    QTextFormat::FontCapitalization,
                             QTextFormat::ForegroundBrush,  QTextFormat::BackgroundBrush,    QTextFormat::TextVerticalAlignment,
                             QTextFormat::FontLetterSpacing, QTextFormat::FontLetterSpacingType, QTextFormat::FontStretch,
                             QTextFormat::TextBaselineOffset, Docx::kUnderlineWord,          Docx::kDoubleStrike,
                             Docx::kHiddenText,             Docx::kKerning,                  Docx::kRunStyleId};
// A paragraph's look; spacing, line height and tab stops have their own
// setters (the folded margins, Word's line metrics, Qt's tab positions).
constexpr int kBlockKeys[] = {QTextFormat::BlockAlignment,  QTextFormat::BlockRightMargin, QTextFormat::BackgroundBrush,
                              QTextFormat::BlockNonBreakableLines, QTextFormat::LayoutDirection, QTextFormat::HeadingLevel,
                              Docx::kKeepWithNext,          Docx::kWidowControl,           Docx::kContextualSpacing,
                              Docx::kOutlineLevel,          Docx::kParagraphBorderSpec,    Docx::kParagraphBorders,
                              Docx::kParagraphStyleId,      Docx::kWriteStyleIdProperty};

template <typename Format>
void take(Format &to, const QTextFormat &from, int key)
{
    if (from.hasProperty(key)) {
        to.setProperty(key, from.property(key));
    } else {
        to.clearProperty(key);
    }
}

} // namespace

FormatPainter::FormatPainter(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                             QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_brush = model->add(QStringLiteral("writeFormatPainterAction"), tr("Format &Painter"), {}, QStringLiteral("paintbrush"));
    m_brush->setCheckable(true);
    m_copy = model->add(QStringLiteral("writeCopyFormatAction"), tr("&Copy Formatting"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+C")), QStringLiteral("copy"));
    m_paste = model->add(QStringLiteral("writePasteFormatAction"), tr("Paste &Formatting"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+V")), QStringLiteral("clipboard-paste"));
    m_paste->setEnabled(false);
    connect(m_brush, &ActionEntry::triggered, this, &FormatPainter::press);
    connect(m_copy, &ActionEntry::triggered, this, &FormatPainter::copy);
    connect(m_paste, &ActionEntry::triggered, this, &FormatPainter::paste);
}

QList<MenuNode> FormatPainter::menuItems() const
{
    return {MenuNode::action(m_brush->id()), MenuNode::action(m_copy->id()), MenuNode::action(m_paste->id())};
}

void FormatPainter::copy()
{
    const QTextCursor cursor = m_control->textCursor();
    QTextCursor first(cursor.document());
    first.setPosition(cursor.hasSelection() ? cursor.selectionStart() + 1 : cursor.position());
    const QTextCharFormat source = cursor.hasSelection() ? first.charFormat() : m_control->currentCharFormat();
    m_char = QTextCharFormat();
    for (const int key : kCharKeys) {
        take(m_char, source, key);
    }
    m_block = cursor.block().blockFormat();
    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    // Word: the paragraph's formatting travels with a whole paragraph (its
    // mark selected) or with the caret alone.
    m_paragraph = !cursor.hasSelection() || blocks.size() > 1
        || (cursor.selectionStart() <= blocks.first().position()
            && cursor.selectionEnd() >= blocks.first().position() + blocks.first().length() - 1);
    m_hasFormat = true;
    m_paste->setEnabled(true);
}

bool FormatPainter::paste()
{
    if (!m_hasFormat) {
        return false;
    }
    QTextCursor cursor = m_control->textCursor();
    QTextCursor runs = cursor;
    if (!runs.hasSelection()) {
        runs.select(QTextCursor::WordUnderCursor); // a click paints the word under it
    }
    QTextDocument *document = cursor.document();
    cursor.beginEditBlock();
    // Run by run: each keeps what it is, takes the brush's look.
    QList<std::pair<std::pair<int, int>, QTextCharFormat>> spans;
    for (QTextBlock block = document->findBlock(runs.selectionStart()); block.isValid() && block.position() <= runs.selectionEnd();
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = qMax(runs.selectionStart(), fragment.position());
            const int to = qMin(runs.selectionEnd(), fragment.position() + fragment.length());
            QTextCharFormat format = fragment.charFormat();
            if (from >= to || format.isImageFormat() || format.hasProperty(Docx::kInlineXml)) {
                continue;
            }
            for (const int key : kCharKeys) {
                take(format, m_char, key);
            }
            spans.append({{from, to}, format});
        }
    }
    for (const auto &[range, format] : std::as_const(spans)) {
        QTextCursor span(document);
        span.setPosition(range.first);
        span.setPosition(range.second, QTextCursor::KeepAnchor);
        span.setCharFormat(format);
    }
    if (!cursor.hasSelection() && !runs.hasSelection()) {
        QTextCharFormat typing = m_control->currentCharFormat();
        for (const int key : kCharKeys) {
            take(typing, m_char, key);
        }
        m_control->setCurrentCharFormat(typing);
    }
    if (m_paragraph) {
        for (const QTextBlock &block : selectedBlocks(cursor)) {
            QTextBlockFormat f = block.blockFormat();
            for (const int key : kBlockKeys) {
                take(f, m_block, key);
            }
            if (block.textList() == nullptr) { // a list item's indents are its level's
                for (const int key : {int(QTextFormat::BlockLeftMargin), int(QTextFormat::TextIndent)}) {
                    take(f, m_block, key);
                }
            }
            const Docx::LineSpacing line = Docx::ownLineSpacing(m_block);
            Docx::setWordLineSpacing(f, block, line.type, line.value);
            Docx::setTabStops(f, Docx::tabStops(m_block));
            QTextCursor(block).setBlockFormat(f);
            Docx::setParagraphSpacing(block, Docx::ownSpacing(m_block));
        }
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
    m_control->setTextCursor(m_control->textCursor());
    return true;
}

void FormatPainter::press()
{
    if (!m_active) {
        copy();
        setActive(true, false);
        m_armed.start();
    } else if (!m_locked && m_armed.isValid() && m_armed.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
        setActive(true, true); // a double click: until Esc
    } else {
        setActive(false, false);
    }
}

void FormatPainter::selectionReleased()
{
    if (!m_active) {
        return;
    }
    paste();
    if (!m_locked) {
        setActive(false, false);
    }
}

bool FormatPainter::handleKey(QKeyEvent *event)
{
    if (!m_active || event->key() != Qt::Key_Escape) {
        return false;
    }
    setActive(false, false);
    return true;
}

void FormatPainter::setActive(bool active, bool locked)
{
    const bool changed = active != m_active || locked != m_locked;
    m_active = active;
    m_locked = active && locked;
    m_brush->setChecked(active);
    if (changed) {
        emit activeChanged();
    }
}

} // namespace QindaOffice::Apps::Write
