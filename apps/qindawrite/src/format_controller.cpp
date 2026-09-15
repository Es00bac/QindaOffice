// SPDX-License-Identifier: GPL-3.0-or-later
#include "format_controller.h"

#include "page_text_view.h"
#include "write_document.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QImage>
#include <QMenu>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>
#include <QToolBar>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

namespace QindaOffice::Apps::Write {

namespace {

const int kFontSizes[] = {6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20,
                          22, 24, 26, 28, 32, 36, 40, 44, 48, 54,
                          60, 66, 72, 80, 88, 96};

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

} // namespace

FormatController::FormatController(PageTextView *view, WriteDocument *document,
                                   QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(document)
    , m_actions(createWriteActions(this))
{
    QWidgetTextControl *control = m_view->control();

    connect(m_actions.bold, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontWeight(on ? QFont::Bold : QFont::Normal);
        applyCharFormat(format);
    });
    connect(m_actions.italic, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontItalic(on);
        applyCharFormat(format);
    });
    connect(m_actions.underline, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontUnderline(on);
        applyCharFormat(format);
    });
    connect(m_actions.strikeOut, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontStrikeOut(on);
        applyCharFormat(format);
    });
    connect(m_actions.textColor, &QAction::triggered, this, [this] { pickTextColor(); });
    connect(m_actions.clearFormat, &QAction::triggered, this,
            [this] { clearDirectFormatting(); });

    connect(m_actions.alignLeft, &QAction::triggered, this,
            [this] { applyAlignment(Qt::AlignLeft); });
    connect(m_actions.alignCenter, &QAction::triggered, this,
            [this] { applyAlignment(Qt::AlignHCenter); });
    connect(m_actions.alignRight, &QAction::triggered, this,
            [this] { applyAlignment(Qt::AlignRight); });
    connect(m_actions.alignJustify, &QAction::triggered, this,
            [this] { applyAlignment(Qt::AlignJustify); });

    connect(m_actions.listBullet, &QAction::triggered, this,
            [this] { toggleList(QTextListFormat::ListDisc); });
    connect(m_actions.listNumbered, &QAction::triggered, this,
            [this] { toggleList(QTextListFormat::ListDecimal); });
    connect(m_actions.indentMore, &QAction::triggered, this, [this] { adjustIndent(1); });
    connect(m_actions.indentLess, &QAction::triggered, this, [this] { adjustIndent(-1); });

    connect(m_actions.insertTable, &QAction::triggered, this, [this] { insertTable(); });
    connect(m_actions.insertImage, &QAction::triggered, this, [this] { insertImage(); });
    connect(m_actions.insertColumns, &QAction::triggered, this,
            [this] { insertColumns(); });
    connect(m_actions.insertPageBreak, &QAction::triggered, this,
            [this] { insertPageBreak(); });

    connect(control, &QWidgetTextControl::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFromControl(); });
    connect(control, &QWidgetTextControl::cursorPositionChanged, this,
            [this] { syncFromControl(); });
    syncFromControl();
}

QToolBar *FormatController::toolBar()
{
    if (m_toolBar != nullptr) {
        return m_toolBar;
    }
    auto *window = qobject_cast<QWidget *>(parent());
    m_toolBar = new QToolBar(tr("Formatting"), window);
    m_toolBar->setObjectName(QStringLiteral("formatToolBar"));

    m_fontCombo = new QFontComboBox(m_toolBar);
    m_fontCombo->setObjectName(QStringLiteral("fontFamilyCombo"));
    m_fontCombo->setMinimumWidth(180);
    m_toolBar->addWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &font) {
                if (m_syncing) {
                    return;
                }
                QTextCharFormat format;
                format.setFontFamilies({font.family()});
                applyCharFormat(format);
            });

    m_sizeCombo = new QComboBox(m_toolBar);
    m_sizeCombo->setObjectName(QStringLiteral("fontSizeCombo"));
    m_sizeCombo->setEditable(true);
    for (const int size : kFontSizes) {
        m_sizeCombo->addItem(QString::number(size));
    }
    m_toolBar->addWidget(m_sizeCombo);
    connect(m_sizeCombo, &QComboBox::activated, this, [this](int) {
        bool ok = false;
        const qreal size = m_sizeCombo->currentText().toDouble(&ok);
        if (!ok || size <= 0) {
            return;
        }
        QTextCharFormat format;
        format.setFontPointSize(size);
        applyCharFormat(format);
    });

    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actions.bold);
    m_toolBar->addAction(m_actions.italic);
    m_toolBar->addAction(m_actions.underline);
    m_toolBar->addAction(m_actions.strikeOut);
    m_toolBar->addAction(m_actions.textColor);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actions.alignLeft);
    m_toolBar->addAction(m_actions.alignCenter);
    m_toolBar->addAction(m_actions.alignRight);
    m_toolBar->addAction(m_actions.alignJustify);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actions.listBullet);
    m_toolBar->addAction(m_actions.listNumbered);
    m_toolBar->addAction(m_actions.indentLess);
    m_toolBar->addAction(m_actions.indentMore);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actions.insertTable);
    m_toolBar->addAction(m_actions.insertImage);
    m_toolBar->addAction(m_actions.insertColumns);

    syncFromControl();
    return m_toolBar;
}

void FormatController::populateFormatMenu(QMenu *menu)
{
    menu->addAction(m_actions.bold);
    menu->addAction(m_actions.italic);
    menu->addAction(m_actions.underline);
    menu->addAction(m_actions.strikeOut);
    menu->addAction(m_actions.textColor);
    menu->addAction(m_actions.clearFormat);
    menu->addSeparator();
    menu->addAction(m_actions.alignLeft);
    menu->addAction(m_actions.alignCenter);
    menu->addAction(m_actions.alignRight);
    menu->addAction(m_actions.alignJustify);
    menu->addSeparator();
    menu->addAction(m_actions.listBullet);
    menu->addAction(m_actions.listNumbered);
    menu->addAction(m_actions.indentLess);
    menu->addAction(m_actions.indentMore);
}

void FormatController::populateInsertMenu(QMenu *menu)
{
    menu->addAction(m_actions.insertPageBreak);
    menu->addAction(m_actions.insertTable);
    menu->addAction(m_actions.insertImage);
    menu->addAction(m_actions.insertColumns);
}

void FormatController::applyCharFormat(const QTextCharFormat &format)
{
    QWidgetTextControl *control = m_view->control();
    const QTextCursor cursor = control->textCursor();
    if (cursor.hasSelection()) {
        QTextCursor edit = cursor;
        edit.mergeCharFormat(format);
    } else {
        QTextCharFormat merged = control->currentCharFormat();
        merged.merge(format);
        control->setCurrentCharFormat(merged);
    }
    m_document->setDirty(true);
}

void FormatController::mergeBlockFormat(const QTextBlockFormat &format)
{
    QTextCursor cursor = m_view->control()->textCursor();
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
    QWidgetTextControl *control = m_view->control();
    const QTextCursor cursor = control->textCursor();
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
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextBlockFormat format;
    format.setIndent(qMax(0, cursor.blockFormat().indent() + delta));
    mergeBlockFormat(format);
}

void FormatController::pickTextColor()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    const QColor initial = cursor.charFormat().foreground().color();
    const QColor color = QColorDialog::getColor(initial, m_view, tr("Text Color"));
    if (!color.isValid()) {
        return;
    }
    QTextCharFormat format;
    format.setForeground(color);
    applyCharFormat(format);
}

void FormatController::clearDirectFormatting()
{
    QWidgetTextControl *control = m_view->control();
    QTextCursor cursor = control->textCursor();
    cursor.beginEditBlock();
    cursor.setCharFormat(QTextCharFormat());
    cursor.setBlockFormat(QTextBlockFormat());
    cursor.endEditBlock();
    control->setCurrentCharFormat(QTextCharFormat());
    m_document->setDirty(true);
}

void FormatController::insertTable()
{
    QDialog dialog(m_view);
    dialog.setWindowTitle(tr("Insert Table"));
    auto *layout = new QFormLayout(&dialog);
    auto *rows = new QSpinBox(&dialog);
    rows->setRange(1, 200);
    rows->setValue(3);
    auto *cols = new QSpinBox(&dialog);
    cols->setRange(1, 20);
    cols->setValue(3);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(tr("Rows"), rows);
    layout->addRow(tr("Columns"), cols);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    insertTableAtCursor(rows->value(), cols->value());
}

void FormatController::insertTableAtCursor(int rows, int columns)
{
    QTextCursor cursor = m_view->control()->textCursor();
    QTextTableFormat format;
    format.setBorder(1);
    format.setCellSpacing(2);
    format.setCellPadding(4);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    cursor.insertTable(rows, columns, format);
    m_document->setDirty(true);
}

void FormatController::insertImage()
{
    const QString path = QFileDialog::getOpenFileName(
        m_view, tr("Insert Image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QImage loaded(path);
    if (loaded.isNull()) {
        return;
    }
    const qreal contentWidth = m_document->pageSize().width() - 2 * m_document->pageMargin();
    if (loaded.width() > contentWidth) {
        loaded = loaded.scaledToWidth(int(contentWidth), Qt::SmoothTransformation);
    }
    QTextCursor cursor = m_view->control()->textCursor();
    QTextImageFormat format;
    format.setName(path);
    format.setWidth(loaded.width());
    format.setHeight(loaded.height());
    cursor.insertImage(format);
    m_document->setDirty(true);
}

void FormatController::insertColumns()
{
    QDialog dialog(m_view);
    dialog.setWindowTitle(tr("Insert Columns"));
    auto *layout = new QFormLayout(&dialog);
    auto *cols = new QSpinBox(&dialog);
    cols->setRange(2, 4);
    cols->setValue(2);
    auto *spacing = new QSpinBox(&dialog);
    spacing->setRange(0, 96);
    spacing->setValue(24);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(tr("Columns"), cols);
    layout->addRow(tr("Gutter (px)"), spacing);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    insertColumnsAtCursor(cols->value(), spacing->value());
}

void FormatController::insertColumnsAtCursor(int columns, int gutterPx)
{
    // AGENT-NOTE: QTextDocument has no native multi-column section layout
    // (unlike ODF text:columns). A borderless 1xN table is the
    // serialization-safe equivalent: it round-trips through HTML, ODT and
    // DOCX and paginates like any other table.
    QTextCursor cursor = m_view->control()->textCursor();
    QTextTableFormat format;
    format.setBorder(0);
    format.setCellSpacing(0);
    format.setCellPadding(gutterPx / 2);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    cursor.insertTable(1, columns, format);
    m_document->setDirty(true);
}

void FormatController::insertPageBreak()
{
    QTextCursor cursor = m_view->control()->textCursor();
    QTextBlockFormat format;
    format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cursor.insertBlock(format);
    m_document->setDirty(true);
}

void FormatController::syncFromControl()
{
    m_syncing = true;
    QWidgetTextControl *control = m_view->control();
    const QTextCursor cursor = control->textCursor();
    const QTextCharFormat format = control->currentCharFormat();

    m_actions.bold->setChecked(format.fontWeight() >= QFont::Bold);
    m_actions.italic->setChecked(format.fontItalic());
    m_actions.underline->setChecked(format.fontUnderline());
    m_actions.strikeOut->setChecked(format.fontStrikeOut());

    if (m_fontCombo != nullptr) {
        m_fontCombo->setCurrentFont(format.font());
    }
    if (m_sizeCombo != nullptr) {
        if (format.fontPointSize() > 0) {
            m_sizeCombo->setCurrentText(QString::number(int(format.fontPointSize())));
        } else {
            // Mixed sizes across the selection: show blank, never a guess.
            m_sizeCombo->setCurrentText(QString());
        }
    }

    const Qt::Alignment alignment =
        cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask;
    m_actions.alignLeft->setChecked(alignment == Qt::AlignLeft);
    m_actions.alignCenter->setChecked(alignment == Qt::AlignHCenter);
    m_actions.alignRight->setChecked(alignment == Qt::AlignRight);
    m_actions.alignJustify->setChecked(alignment == Qt::AlignJustify);

    QTextList *list = cursor.block().textList();
    const int style =
        list != nullptr ? list->format().style() : QTextListFormat::ListStyleUndefined;
    m_actions.listBullet->setChecked(style == QTextListFormat::ListDisc);
    m_actions.listNumbered->setChecked(style == QTextListFormat::ListDecimal);

    m_syncing = false;
}

} // namespace QindaOffice::Apps::Write
