// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_host.h"

#include "format_controller.h"
#include "format_io.h"
#include "page_text_accessible.h"
#include "page_text_control.h"
#include "page_text_item.h"
#include "pdf_exporter.h"
#include "preferences.h"
#include "spell_checker.h"
#include "standard_actions.h"
#include "style_catalog.h"
#include "table_actions.h"
#include "write_review_actions.h"
#include "write_revisions.h"
#include "write_typography.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QPrinter>
#include <QTextBlock>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {
const int kFontSizes[] = {6,  7,  8,  9,  10, 11, 12, 14, 16, 18, 20, 22, 24, 26,
                          28, 32, 36, 40, 44, 48, 54, 60, 66, 72, 80, 88, 96};
}

WriteHost::WriteHost()
    : m_document(new WriteDocument)
    , m_control(new PageTextControl(m_document->textDocument(), this))
{
    installPageTextAccessibility();
    setDocument(m_document);
    m_spell = new SpellChecker(m_document->textDocument());
    m_spell->setParent(this);
    // The controllers own their command sets; the window renders them.
    m_format = new FormatController(m_control, m_document, actions(), this);
    m_tables = new TableController(m_control, m_document, actions(), this);
    m_typography = new WriteTypography(m_control, m_document, actions(), this);
    m_revisions = new WriteRevisions(m_control, m_document, this);
    m_review = new WriteReviewActions(m_control, m_document, m_revisions, actions(), this);
    connect(m_format, &FormatController::textColorRequested, this, &WriteHost::textColorRequested);
    connect(m_format, &FormatController::tableDialogRequested, this, &WriteHost::tableDialogRequested);
    connect(m_format, &FormatController::columnsDialogRequested, this, &WriteHost::columnsDialogRequested);
    connect(m_format, &FormatController::imageDialogRequested, this, &WriteHost::imageDialogRequested);
    connect(m_format, &FormatController::formatChanged, this, &WriteHost::formatChanged);
    connect(m_typography, &WriteTypography::highlightRequested, this, &WriteHost::highlightRequested);
    connect(m_typography, &WriteTypography::symbolRequested, this, &WriteHost::symbolRequested);
    connect(m_typography, &WriteTypography::hyperlinkRequested, this, &WriteHost::hyperlinkRequested);
    connect(m_typography, &WriteTypography::wordCountRequested, this, &WriteHost::wordCountRequested);
    connect(m_typography, &WriteTypography::countsChanged, this, [this] { updateWriteStatus(); });
    connect(m_review, &WriteReviewActions::footnoteRequested, this, &WriteHost::footnoteRequested);
    connect(m_review, &WriteReviewActions::message, this,
            [this](const QString &text) { showStatusMessage(text, 6000); });
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] { emit rulerChanged(); });
    connect(m_control, &PageTextControl::doubleClicked, this,
            [this](int position) { static_cast<void>(m_review->editFootnoteAt(position)); });
    connect(m_document, &WriteDocument::pageSetupChanged, this, [this] {
        emit pageSetupChanged();
        emit rulerChanged();
    });
    createTextActions();
    createAssistant();
    createSpellingEntries();
    insertMenus();
    wireEditCommands();
    finalizeHost();
    updateWriteStatus();
}

WriteHost::~WriteHost()
{
    // The control borrows the document; it must die before the host's
    // document pointer goes away with the base class.
    delete m_control;
    m_control = nullptr;
}

void WriteHost::attachPage(PageTextItem *item)
{
    m_page = item;
    if (item == nullptr) {
        return;
    }
    item->attach(m_document, m_control);
    m_revisions->setInputTarget(item);
    connect(item, &PageTextItem::pageCountChanged, this, [this] { updateWriteStatus(); });
    connect(item, &PageTextItem::viewZoomChanged, this, [this] { updateWriteStatus(); });
    connect(item, &PageTextItem::contextMenuRequested, this,
            [this](qreal x, qreal y, const QPointF &docPos) {
                prepareSpellingMenu(docPos);
                emit contextMenuRequested(x, y);
            });
    applyPreferences();
    updateWriteStatus();
}

// ---- the shell's edit commands -----------------------------------------------------

void WriteHost::wireEditCommands()
{
    ActionEntry *undo = action(QString::fromLatin1(ActionId::kEditUndo));
    ActionEntry *redo = action(QString::fromLatin1(ActionId::kEditRedo));
    ActionEntry *cut = action(QString::fromLatin1(ActionId::kEditCut));
    ActionEntry *copy = action(QString::fromLatin1(ActionId::kEditCopy));
    ActionEntry *paste = action(QString::fromLatin1(ActionId::kEditPaste));
    ActionEntry *selectAll = action(QString::fromLatin1(ActionId::kEditSelectAll));
    connect(undo, &ActionEntry::triggered, m_control, &PageTextControl::undo);
    connect(redo, &ActionEntry::triggered, m_control, &PageTextControl::redo);
    connect(cut, &ActionEntry::triggered, m_control, &PageTextControl::cut);
    connect(copy, &ActionEntry::triggered, m_control, &PageTextControl::copy);
    connect(paste, &ActionEntry::triggered, m_control, &PageTextControl::paste);
    connect(selectAll, &ActionEntry::triggered, m_control, &PageTextControl::selectAll);
    // Undo/cut/copy follow the control's availability instead of staying on.
    undo->setEnabled(false);
    redo->setEnabled(false);
    cut->setEnabled(false);
    copy->setEnabled(false);
    selectAll->setEnabled(true);
    connect(m_control, &PageTextControl::undoAvailable, undo, &ActionEntry::setEnabled);
    connect(m_control, &PageTextControl::redoAvailable, redo, &ActionEntry::setEnabled);
    connect(m_control, &PageTextControl::copyAvailable, cut, &ActionEntry::setEnabled);
    connect(m_control, &PageTextControl::copyAvailable, copy, &ActionEntry::setEnabled);
    const auto updatePaste = [this, paste] { paste->setEnabled(m_control->canPaste()); };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
}

// ---- status -----------------------------------------------------------------------

void WriteHost::updateWriteStatus()
{
    const WriteTypography::Counts counts = m_typography->counts();
    QString info = counts.selectionOnly
        ? tr("Selected: %1 words, %2 characters").arg(counts.words).arg(counts.characters)
        : tr("%1 words   %2 characters").arg(counts.words).arg(counts.characters);
    const int pages = m_page != nullptr ? m_page->pageCount() : 1;
    const qreal zoom = m_page != nullptr ? m_page->viewZoom() : 1.0;
    info += tr("   Pages: %1").arg(pages);
    info += tr("   Zoom: %1%").arg(qRound(zoom * 100));
    setStatusInfo(info);
}

// ---- format strip -----------------------------------------------------------------

QStringList WriteHost::fontFamilies() const
{
    return QFontDatabase::families();
}

QStringList WriteHost::fontSizes() const
{
    QStringList sizes;
    for (const int size : kFontSizes) {
        sizes.append(QString::number(size));
    }
    return sizes;
}

QString WriteHost::fontFamily() const
{
    return m_format->fontFamily();
}

QString WriteHost::fontSize() const
{
    return QString::number(int(m_format->fontPointSize()));
}

void WriteHost::applyFontFamily(const QString &family)
{
    if (family.isEmpty() || family == fontFamily()) {
        return;
    }
    QTextCharFormat format;
    format.setFontFamilies({family});
    m_format->applyCharFormat(format);
}

void WriteHost::applyFontSize(const QString &text)
{
    bool ok = false;
    const qreal size = text.trimmed().toDouble(&ok);
    if (!ok || size <= 0 || size > 400) {
        return;
    }
    QTextCharFormat format;
    format.setFontPointSize(size);
    m_format->applyCharFormat(format);
}

void WriteHost::applyTextColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    QTextCharFormat format;
    format.setForeground(color);
    m_format->applyCharFormat(format);
}

void WriteHost::applyHighlightColor(const QColor &color)
{
    m_typography->applyHighlight(color);
}

void WriteHost::applyStyle(int styleId)
{
    StyleCatalog catalog;
    // AGENT-GUARD: style the control's live cursor, not a fresh cursor over
    // the document — a document-constructed cursor has no selection, which
    // made every style application land on the first paragraph.
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    const int start = qMin(cursor.selectionStart(), cursor.selectionEnd());
    const int end = qMax(cursor.selectionStart(), cursor.selectionEnd());
    for (QTextBlock block = m_document->textDocument()->begin(); block.isValid(); block = block.next()) {
        if (block.position() > end) {
            break;
        }
        if (block.position() >= start || block.contains(start)) {
            QTextCursor blockCursor(block);
            catalog.apply(blockCursor, styleId);
        }
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
}

// ---- page setup ---------------------------------------------------------------------

QVariantMap WriteHost::pageSetup() const
{
    // Document px throughout; the dialog shows inches or centimetres.
    const Docx::PageLayout &l = m_document->pageLayout();
    return {{QStringLiteral("width"), l.pageSize.width()},
            {QStringLiteral("height"), l.pageSize.height()},
            {QStringLiteral("left"), l.margins.left()},
            {QStringLiteral("top"), l.margins.top()},
            {QStringLiteral("right"), l.margins.right()},
            {QStringLiteral("bottom"), l.margins.bottom()},
            {QStringLiteral("headerDistance"), l.headerDistance},
            {QStringLiteral("footerDistance"), l.footerDistance},
            {QStringLiteral("landscape"), l.landscape()},
            {QStringLiteral("differentFirstPage"), m_document->differentFirstPage()},
            {QStringLiteral("header"), m_document->headerText()},
            {QStringLiteral("footer"), m_document->footerText()},
            {QStringLiteral("headerRich"), !m_document->headerIsSimple()},
            {QStringLiteral("footerRich"), !m_document->footerIsSimple()}};
}

void WriteHost::applyPageSetup(const QVariantMap &setup)
{
    Docx::PageLayout layout = m_document->pageLayout();
    const qreal width = setup.value(QStringLiteral("width"), layout.pageSize.width()).toReal();
    const qreal height = setup.value(QStringLiteral("height"), layout.pageSize.height()).toReal();
    if (width <= 0 || height <= 0) {
        return;
    }
    layout.pageSize = QSizeF(width, height);
    auto margin = [&](const char *key, qreal current) {
        return qBound<qreal>(0, setup.value(QString::fromLatin1(key), current).toReal(), qMin(width, height) / 2);
    };
    layout.margins = QMarginsF(margin("left", layout.margins.left()), margin("top", layout.margins.top()),
                               margin("right", layout.margins.right()), margin("bottom", layout.margins.bottom()));
    layout.headerDistance = margin("headerDistance", layout.headerDistance);
    layout.footerDistance = margin("footerDistance", layout.footerDistance);
    m_document->setPageLayout(layout);
    m_document->setDifferentFirstPage(setup.value(QStringLiteral("differentFirstPage"),
                                                  m_document->differentFirstPage()).toBool());
    m_document->setHeaderFooter(setup.value(QStringLiteral("header"), m_document->headerText()).toString(),
                                setup.value(QStringLiteral("footer"), m_document->footerText()).toString());
    m_document->setDirty(true);
}

// ---- hooks --------------------------------------------------------------------------

QString WriteHost::filterString() const
{
    return openFilterString();
}

QString WriteHost::saveFilterString() const
{
    // Default Save format is ODT (plan 6.1).
    return defaultSaveFilter();
}

void WriteHost::applyPreferences()
{
    const Quick::Preferences &prefs = Quick::Preferences::instance();
    QTextDocument *text = m_document->textDocument();
    QFont documentFont = text->defaultFont();
    if (!prefs.documentFontFamily().isEmpty()) {
        documentFont.setFamily(prefs.documentFontFamily());
    }
    if (prefs.documentFontSize() > 0) {
        documentFont.setPointSizeF(prefs.documentFontSize());
    }
    if (documentFont != text->defaultFont()) {
        text->setDefaultFont(documentFont);
    }
    emit zoomRequested(prefs.defaultZoomPercent() / 100.0);
    m_spell->setEnabled(prefs.spellCheckEnabled());
    updateWriteStatus();
    emit formatChanged();
}

void WriteHost::documentReplaced()
{
    QTextCursor cursor(m_document->textDocument());
    m_control->setTextCursor(cursor);
    emit pageSetupChanged();
    emit rulerChanged();
    updateWriteStatus();
    // What the open approximated (text boxes, charts, …) is said once, not
    // hidden: those objects are kept unchanged on save (D-068).
    const QStringList warnings = m_document->openWarnings();
    if (!warnings.isEmpty()) {
        showStatusMessage(warnings.join(QLatin1Char(' ')), 12000);
    }
}

Quick::PrintRenderCallback WriteHost::printRenderCallback() const
{
    const WriteDocument *doc = m_document;
    return [doc](QPrinter *printer, QPainter *painter) {
        // WYSIWYG: the paper follows the document's page size, matching the
        // on-screen and PDF pagination.
        // Document px → points (×0.75): see the AGENT-GUARD in pdf_exporter.cpp.
        const QSizeF docPage = doc->pageSize();
        if (docPage.width() > 0 && docPage.height() > 0) {
            printer->setPageSize(QPageSize(docPage * 0.75, QPageSize::Point));
            printer->setPageMargins(QMarginsF(0, 0, 0, 0));
        }
        renderDocumentPages(painter, QSizeF(printer->width(), printer->height()),
                            const_cast<QTextDocument *>(doc->textDocument()),
                            [printer] { printer->newPage(); }, doc->decoration());
    };
}

} // namespace QindaOffice::Apps::Write
