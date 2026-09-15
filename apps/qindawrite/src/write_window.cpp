// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_window.h"

#include "page_text_view.h"
#include "pdf_exporter.h"
#include "format_io.h"
#include "print_helper.h"
#include "spell_checker.h"
#include "style_catalog.h"
#include "write_document.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QInputDialog>
#include <QPrinter>
#include <QTextBlock>
#include <QTextList>
#include <QTextTable>
#include <QtWidgets/private/qwidgettextcontrol_p.h>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

WriteWindow::WriteWindow()
    : m_document(new WriteDocument)
    , m_view(new PageTextView(m_document))
    , m_spell(new SpellChecker(m_document->textDocument()))
{
    setDocumentHost(m_view, m_document);

    // Print and print preview share the PDF exporter's pagination loop, so
    // paper matches the screen by construction (plan 6.1).
    m_actions.filePrint->setEnabled(true);
    connect(m_actions.filePrint, &QAction::triggered, this, [this] {
        Shell::PrintHelper::print(this, printRenderCallback());
    });
    m_actions.filePrintPreview->setEnabled(true);
    connect(m_actions.filePrintPreview, &QAction::triggered, this, [this] {
        Shell::PrintHelper::printPreview(this, printRenderCallback());
    });
    connect(m_view, &PageTextView::contextMenuBuilt, this, &WriteWindow::addSpellingActions);
}

WriteWindow::~WriteWindow() = default;

void WriteWindow::exportPdf()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (exportDocumentPdf(m_document->textDocument(), path, &error)) {
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Export failed: %1").arg(error), 8000);
    }
}

void WriteWindow::applyStyle(int styleId)
{
    StyleCatalog catalog;
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    QTextBlock block = m_document->textDocument()->begin();
    const int start = qMin(cursor.selectionStart(), cursor.selectionEnd());
    const int end = qMax(cursor.selectionStart(), cursor.selectionEnd());
    while (block.isValid()) {
        if (block.position() >= start && block.position() <= end) {
            QTextCursor blockCursor(block);
            catalog.apply(blockCursor, styleId);
        }
        if (block.position() > end) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
}

void WriteWindow::applyCharFormat(const QTextCharFormat &format)
{
    QTextCursor cursor = m_view->control()->textCursor();
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(format);
    } else {
        QTextCharFormat merged = m_view->control()->currentCharFormat();
        merged.merge(format);
        m_view->control()->setCurrentCharFormat(merged);
    }
    m_document->setDirty(true);
}

Shell::PrintRenderCallback WriteWindow::printRenderCallback() const
{
    return [this](QPrinter *printer, QPainter *painter) {
        renderDocumentPages(painter, QSizeF(printer->width(), printer->height()),
                            m_document->textDocument(),
                            [printer] { printer->newPage(); });
    };
}

void WriteWindow::addSpellingActions(QMenu *menu, const QPointF &docPos)
{
    if (m_spell == nullptr || !m_spell->isActive()) {
        return;
    }
    const QTextCursor click = m_view->control()->cursorForPosition(docPos);
    QTextCursor word = click;
    word.select(QTextCursor::WordUnderCursor);
    const QString text = word.selectedText();
    if (text.size() < 2 || !m_spell->isMisspelled(text)) {
        return;
    }
    QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    const QStringList suggestions = m_spell->suggestions(text);
    if (!suggestions.isEmpty()) {
        auto *suggestMenu = new QMenu(tr("Spelling"), menu);
        for (const QString &suggestion : suggestions.mid(0, 7)) {
            QAction *action = suggestMenu->addAction(suggestion);
            connect(action, &QAction::triggered, this, [this, word, suggestion]() mutable {
                word.insertText(suggestion);
                m_document->setDirty(true);
            });
        }
        menu->insertMenu(first, suggestMenu);
    }
    QAction *ignore = new QAction(tr("Ignore '%1'").arg(text), menu);
    connect(ignore, &QAction::triggered, this, [text] {
        // Session-scoped ignore in v1 (no custom dictionary management).
        Q_UNUSED(text);
    });
    if (first != nullptr) {
        menu->insertAction(first, ignore);
    } else {
        menu->addAction(ignore);
    }
}

void WriteWindow::insertApplicationMenus(QMenu *before)
{
    auto *formatMenu = new QMenu(tr("F&ormat"), this);
    menuBar()->insertMenu(before->menuAction(), formatMenu);

    auto *styleMenu = new QMenu(tr("&Style"), this);
    formatMenu->addMenu(styleMenu);
    StyleCatalog catalog;
    const QStringList names = catalog.names();
    for (int id = 0; id < names.size(); ++id) {
        QAction *action = styleMenu->addAction(names.at(id));
        connect(action, &QAction::triggered, this, [this, id] { applyStyle(id); });
    }
    formatMenu->addSeparator();
    QAction *bold = formatMenu->addAction(tr("&Bold"));
    bold->setCheckable(true);
    connect(bold, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontWeight(on ? QFont::Bold : QFont::Normal);
        applyCharFormat(format);
    });
    QAction *italic = formatMenu->addAction(tr("&Italic"));
    italic->setCheckable(true);
    connect(italic, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontItalic(on);
        applyCharFormat(format);
    });
    QAction *underline = formatMenu->addAction(tr("&Underline"));
    underline->setCheckable(true);
    connect(underline, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat format;
        format.setFontUnderline(on);
        applyCharFormat(format);
    });

    auto *insertMenu = new QMenu(tr("&Insert"), this);
    menuBar()->insertMenu(before->menuAction(), insertMenu);
    QAction *pageBreak = insertMenu->addAction(tr("&Page Break"));
    connect(pageBreak, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_view->control()->textCursor();
        QTextBlockFormat format;
        format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        cursor.insertBlock(format);
        m_document->setDirty(true);
    });
    QAction *table = insertMenu->addAction(tr("&Table…"));
    connect(table, &QAction::triggered, this, [this] {
        bool ok = false;
        const int rows = QInputDialog::getInt(this, tr("Insert Table"), tr("Rows"), 3, 1, 200, 1, &ok);
        if (!ok) {
            return;
        }
        const int cols = QInputDialog::getInt(this, tr("Insert Table"), tr("Columns"), 3, 1, 20, 1, &ok);
        if (!ok) {
            return;
        }
        QTextCursor cursor = m_view->control()->textCursor();
        QTextTableFormat format;
        format.setBorder(1);
        format.setCellSpacing(2);
        format.setCellPadding(4);
        format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        cursor.insertTable(rows, cols, format);
        m_document->setDirty(true);
    });
    QAction *image = insertMenu->addAction(tr("&Image…"));
    connect(image, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Insert Image"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QImage loaded(path);
        if (loaded.isNull()) {
            statusBar()->showMessage(tr("Could not load image %1").arg(path), 6000);
            return;
        }
        // Scale to the content width on insert (plan 6.1).
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
    });
    QAction *bullets = insertMenu->addAction(tr("&Bullet List"));
    connect(bullets, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_view->control()->textCursor();
        QTextListFormat format;
        format.setStyle(QTextListFormat::ListDisc);
        cursor.createList(format);
        m_document->setDirty(true);
    });
    QAction *numbered = insertMenu->addAction(tr("&Numbered List"));
    connect(numbered, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_view->control()->textCursor();
        QTextListFormat format;
        format.setStyle(QTextListFormat::ListDecimal);
        cursor.createList(format);
        m_document->setDirty(true);
    });

    auto *pageMenu = new QMenu(tr("&Page"), this);
    menuBar()->insertMenu(before->menuAction(), pageMenu);
    QAction *pageSetup = pageMenu->addAction(tr("&Page Setup…"));
    connect(pageSetup, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        auto *layout = new QFormLayout(&dialog);
        auto *size = new QComboBox(&dialog);
        size->addItem(QStringLiteral("A4"), QSizeF(794, 1123));
        size->addItem(QStringLiteral("Letter"), QSizeF(816, 1056));
        auto *margin = new QDoubleSpinBox(&dialog);
        margin->setRange(0, 200);
        margin->setValue(m_document->pageMargin());
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(tr("Paper"), size);
        layout->addRow(tr("Margin (px)"), margin);
        layout->addRow(buttons);
        if (dialog.exec() == QDialog::Accepted) {
            m_document->setupPaged(size->currentData().toSizeF(), margin->value());
            m_view->geometry()->setPageSize(size->currentData().toSizeF());
            m_view->viewport()->update();
            m_document->setDirty(true);
        }
    });
    QAction *exportAction = pageMenu->addAction(tr("&Export PDF…"));
    connect(exportAction, &QAction::triggered, this, &WriteWindow::exportPdf);
}

QString WriteWindow::filterString() const
{
    return openFilterString();
}

QString WriteWindow::saveFilterString() const
{
    // Default Save format is ODT (plan 6.1).
    return defaultSaveFilter();
}

} // namespace QindaOffice::Apps::Write
