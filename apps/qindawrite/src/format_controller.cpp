// SPDX-License-Identifier: GPL-3.0-or-later
#include "format_controller.h"

#include <QFile>

#include "docx_properties.h"
#include "docx_table_format.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QImage>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// Removes list membership from every block in [start, end]: the block's
// objectIndex points at the list format; -1 detaches it (Qt's canonical
// un-list trick, also used by the textedit example).
void removeFromList(QTextDocument *doc, int start, int end)
{
    QTextBlock block = doc->findBlock(start);
    const QTextBlock last = doc->findBlock(end);
    while (block.isValid()) {
        QTextCursor blockCursor(block);
        QTextBlockFormat format;
        format.setObjectIndex(-1);
        blockCursor.setBlockFormat(format);
        if (block == last) {
            break;
        }
        block = block.next();
    }
}

MenuNode a(ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

} // namespace

FormatController::FormatController(PageTextControl *control, WriteDocument *document,
                                   Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
    , m_actions(createWriteActions(model))
{
    const auto toggleFormat = [this](ActionEntry *entry, auto formatFor) {
        connect(entry, &ActionEntry::toggled, this, [this, formatFor](bool on) {
            if (!m_syncing) {
                applyCharFormat(formatFor(on));
            }
        });
    };
    toggleFormat(m_actions.bold, [](bool on) {
        QTextCharFormat f;
        f.setFontWeight(on ? QFont::Bold : QFont::Normal);
        return f;
    });
    toggleFormat(m_actions.italic, [](bool on) {
        QTextCharFormat f;
        f.setFontItalic(on);
        return f;
    });
    toggleFormat(m_actions.underline, [](bool on) {
        QTextCharFormat f;
        f.setFontUnderline(on);
        // A Word underline Qt cannot draw (double, thick…) is not what
        // Ctrl+U turns back on: Word's toggle gives a single line.
        f.setProperty(Docx::kUnderlineWord, QString());
        return f;
    });
    toggleFormat(m_actions.strikeOut, [](bool on) {
        QTextCharFormat f;
        f.setFontStrikeOut(on);
        return f;
    });
    connect(m_actions.textColor, &ActionEntry::triggered, this, [this] {
        emit textColorRequested(m_control->textCursor().charFormat().foreground().color());
    });
    connect(m_actions.clearFormat, &ActionEntry::triggered, this, [this] { clearDirectFormatting(); });

    const struct {
        ActionEntry *entry;
        Qt::Alignment alignment;
    } aligns[] = {{m_actions.alignLeft, Qt::AlignLeft},
                  {m_actions.alignCenter, Qt::AlignHCenter},
                  {m_actions.alignRight, Qt::AlignRight},
                  {m_actions.alignJustify, Qt::AlignJustify}};
    for (const auto &align : aligns) {
        connect(align.entry, &ActionEntry::triggered, this, [this, alignment = align.alignment] {
            if (!m_syncing) {
                applyAlignment(alignment);
            }
        });
    }
    connect(m_actions.listBullet, &ActionEntry::triggered, this,
            [this] { toggleList(QTextListFormat::ListDisc); });
    connect(m_actions.listNumbered, &ActionEntry::triggered, this,
            [this] { toggleList(QTextListFormat::ListDecimal); });
    connect(m_actions.indentMore, &ActionEntry::triggered, this, [this] { adjustIndent(1); });
    connect(m_actions.indentLess, &ActionEntry::triggered, this, [this] { adjustIndent(-1); });

    connect(m_actions.insertTable, &ActionEntry::triggered, this, &FormatController::tableDialogRequested);
    connect(m_actions.insertImage, &ActionEntry::triggered, this, &FormatController::imageDialogRequested);
    connect(m_actions.insertColumns, &ActionEntry::triggered, this,
            &FormatController::columnsDialogRequested);
    connect(m_actions.insertPageBreak, &ActionEntry::triggered, this, [this] { insertPageBreak(); });

    connect(m_control, &PageTextControl::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFromControl(); });
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] { syncFromControl(); });
    syncFromControl();
}

QList<MenuNode> FormatController::formatMenuItems() const
{
    return {a(m_actions.bold),        a(m_actions.italic),      a(m_actions.underline),
            a(m_actions.strikeOut),   a(m_actions.textColor),   a(m_actions.clearFormat),
            MenuNode::separator(),    a(m_actions.alignLeft),   a(m_actions.alignCenter),
            a(m_actions.alignRight),  a(m_actions.alignJustify), MenuNode::separator(),
            a(m_actions.listBullet),  a(m_actions.listNumbered), a(m_actions.indentLess),
            a(m_actions.indentMore)};
}

QList<MenuNode> FormatController::insertMenuItems() const
{
    return {a(m_actions.insertPageBreak), a(m_actions.insertTable), a(m_actions.insertImage),
            a(m_actions.insertColumns)};
}

QString FormatController::fontFamily() const
{
    const QTextCharFormat format = m_control->currentCharFormat();
    const QStringList families = format.fontFamilies().toStringList();
    if (!families.isEmpty() && !families.first().isEmpty()) {
        return families.first();
    }
    return m_document->textDocument()->defaultFont().family();
}

qreal FormatController::fontPointSize() const
{
    // AGENT-GUARD: never blank. A pristine document has no explicit point
    // size on its char format; fall back to the document's default font.
    const qreal explicitSize = m_control->currentCharFormat().fontPointSize();
    if (explicitSize > 0) {
        return explicitSize;
    }
    const qreal fallback = m_document->textDocument()->defaultFont().pointSizeF();
    return fallback > 0 ? fallback : 11.0;
}

void FormatController::applyCharFormat(const QTextCharFormat &format)
{
    const QTextCursor cursor = m_control->textCursor();
    if (cursor.hasSelection()) {
        QTextCursor edit = cursor;
        edit.mergeCharFormat(format);
    } else {
        QTextCharFormat merged = m_control->currentCharFormat();
        merged.merge(format);
        m_control->setCurrentCharFormat(merged);
    }
    m_document->setDirty(true);
    syncFromControl();
}

void FormatController::mergeBlockFormat(const QTextBlockFormat &format)
{
    QTextCursor cursor = m_control->textCursor();
    cursor.mergeBlockFormat(format);
    m_document->setDirty(true);
}

void FormatController::applyAlignment(Qt::Alignment alignment)
{
    QTextBlockFormat format;
    format.setAlignment(alignment);
    mergeBlockFormat(format);
    syncFromControl();
}

void FormatController::toggleList(QTextListFormat::Style style)
{
    const QTextCursor cursor = m_control->textCursor();
    QTextDocument *doc = m_document->textDocument();
    QTextList *list = cursor.block().textList();
    if (list != nullptr && list->format().style() == style) {
        removeFromList(doc, cursor.selectionStart(), cursor.selectionEnd());
    } else {
        QTextCursor edit = cursor;
        edit.beginEditBlock();
        removeFromList(doc, cursor.selectionStart(), cursor.selectionEnd());
        QTextListFormat listFormat;
        listFormat.setStyle(style);
        edit.createList(listFormat);
        edit.endEditBlock();
    }
    m_document->setDirty(true);
    syncFromControl();
}

void FormatController::adjustIndent(int delta)
{
    const QTextCursor cursor = m_control->textCursor();
    QTextBlockFormat format;
    format.setIndent(qMax(0, cursor.blockFormat().indent() + delta));
    mergeBlockFormat(format);
}

void FormatController::clearDirectFormatting()
{
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    cursor.setCharFormat(QTextCharFormat());
    cursor.setBlockFormat(QTextBlockFormat());
    cursor.endEditBlock();
    m_control->setCurrentCharFormat(QTextCharFormat());
    m_document->setDirty(true);
    syncFromControl();
}

void FormatController::insertTableAtCursor(int rows, int columns)
{
    // Word's Insert Table (M70, D-079): Table Grid's 0.5 pt lines, equal
    // fixed columns across the text column, 0.08 in cell margins — the
    // model the border, shading and layout commands work on.
    QTextCursor cursor = m_control->textCursor();
    columns = qBound(1, columns, 63);
    cursor.beginEditBlock();
    QTextTable *table = cursor.insertTable(qBound(1, rows, 200), columns,
                                           Docx::newWordTableFormat(columns, m_document->contentWidth()));
    Docx::initWordTable(table);
    cursor.endEditBlock();
    // Leave the cursor inside the first cell so table-scoped commands
    // enable immediately.
    m_control->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    m_document->setDirty(true);
}

void FormatController::insertColumnsAtCursor(int columns, int gutterPx)
{
    // AGENT-NOTE: QTextDocument has no native multi-column section layout
    // (unlike ODF text:columns). A borderless 1xN table is the
    // serialization-safe equivalent: it round-trips through HTML, ODT and
    // DOCX and paginates like any other table.
    QTextCursor cursor = m_control->textCursor();
    QTextTableFormat format;
    format.setBorder(0);
    format.setCellSpacing(0);
    format.setCellPadding(gutterPx / 2);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QTextTable *table = cursor.insertTable(1, qBound(2, columns, 4), format);
    m_control->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    m_document->setDirty(true);
}

bool FormatController::insertImageFile(const QString &path)
{
    // AGENT-NOTE (D-068): the picture's BYTES join the document (as Word
    // embeds them); referring to the file by path broke the moment the file
    // moved, and every .docx/.html save lost it.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QImage loaded = QImage::fromData(bytes);
    const QString name = m_document->addPicture(bytes);
    if (loaded.isNull() || name.isEmpty()) {
        return false;
    }
    QSizeF size(loaded.width(), loaded.height());
    const qreal contentWidth = m_document->contentWidth();
    if (size.width() > contentWidth && contentWidth > 0) {
        size = QSizeF(contentWidth, size.height() * contentWidth / size.width());
    }
    QTextCursor cursor = m_control->textCursor();
    QTextImageFormat format;
    format.setName(name);
    format.setWidth(size.width());
    format.setHeight(size.height());
    cursor.insertImage(format);
    m_document->setDirty(true);
    return true;
}

void FormatController::insertPageBreak()
{
    QTextCursor cursor = m_control->textCursor();
    QTextBlockFormat format;
    format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(format);
    m_document->setDirty(true);
}

void FormatController::syncFromControl()
{
    m_syncing = true;
    const QTextCursor cursor = m_control->textCursor();
    const QTextCharFormat format = m_control->currentCharFormat();
    m_actions.bold->setChecked(format.fontWeight() >= QFont::Bold);
    m_actions.italic->setChecked(format.fontItalic());
    m_actions.underline->setChecked(format.fontUnderline());
    m_actions.strikeOut->setChecked(format.fontStrikeOut());

    const Qt::Alignment alignment = cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask;
    m_actions.alignLeft->setChecked(alignment == Qt::AlignLeft || alignment == 0);
    m_actions.alignCenter->setChecked(alignment == Qt::AlignHCenter);
    m_actions.alignRight->setChecked(alignment == Qt::AlignRight);
    m_actions.alignJustify->setChecked(alignment == Qt::AlignJustify);

    QTextList *list = cursor.block().textList();
    const int style = list != nullptr ? list->format().style() : QTextListFormat::ListStyleUndefined;
    m_actions.listBullet->setChecked(style == QTextListFormat::ListDisc);
    m_actions.listNumbered->setChecked(style == QTextListFormat::ListDecimal);
    m_syncing = false;
    emit formatChanged();
}

} // namespace QindaOffice::Apps::Write
