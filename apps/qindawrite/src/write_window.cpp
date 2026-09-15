// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_window.h"

#include "page_text_view.h"
#include "pdf_exporter.h"
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
#include <QTextBlock>
#include <QtWidgets/private/qwidgettextcontrol_p.h>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

WriteWindow::WriteWindow()
    : m_document(new WriteDocument)
    , m_view(new PageTextView(m_document))
{
    setDocumentHost(m_view, m_document);
}

WriteWindow::~WriteWindow() = default;

void WriteWindow::applyStyle(int styleId)
{
    StyleCatalog catalog;
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    // Apply to every selected block.
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
        m_view->control()->setCurrentCharFormat(
            [this, &format] {
                QTextCharFormat merged = m_view->control()->currentCharFormat();
                merged.merge(format);
                return merged;
            }());
    }
    m_document->setDirty(true);
}

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
    // M5 expands to the full matrix; native save format is HTML for now.
    return tr("Documents (*.html *.htm);;All files (*)");
}

} // namespace QindaOffice::Apps::Write
