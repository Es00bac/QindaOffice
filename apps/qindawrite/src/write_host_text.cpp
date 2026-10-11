// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost's text side: the find/replace commands over the control
// (wrapping once, reporting honestly), the dialog answers (table,
// columns, image, symbol, hyperlink, footnote, PDF export), the ruler's
// markers and New from Template. Split from write_host.cpp for the line
// budget.

#include "write_host.h"

#include "docx_sections.h"
#include "write_sections.h"
#include "write_notes.h"

#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "write_selection.h"

#include "format_controller.h"
#include "page_text_control.h"
#include "pdf_exporter.h"
#include "standard_actions.h"
#include "write_review_actions.h"
#include "write_typography.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

void WriteHost::createTextActions()
{
    for (const char *id : {ActionId::kEditFind, ActionId::kEditFindNext, ActionId::kEditFindPrevious,
                           ActionId::kEditReplace, ActionId::kViewZoomIn, ActionId::kViewZoomOut,
                           ActionId::kViewZoomReset}) {
        action(QString::fromLatin1(id))->setEnabled(true);
    }
    connect(action(QString::fromLatin1(ActionId::kEditFind)), &ActionEntry::triggered, this,
            [this] { emit findRequested(false); });
    connect(action(QString::fromLatin1(ActionId::kEditReplace)), &ActionEntry::triggered, this,
            [this] { emit findRequested(true); });
    // Find Next/Previous re-open the bar; the bar answers them once it is
    // up (WriteWindow.qml wires the entries to its find bar).
}

// ---- find and replace ---------------------------------------------------------------

bool WriteHost::findIn(const QString &needle, bool caseSensitive, bool backward)
{
    if (needle.isEmpty()) {
        return false;
    }
    QTextDocument::FindFlags flags;
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (m_control->find(needle, flags)) {
        return true;
    }
    // Wrap around once, then report honestly. The user's caret is only
    // moved when the wrap actually finds something (an earlier release
    // jumped it to document start on any failed search).
    const QTextCursor restore = m_control->textCursor();
    QTextCursor cursor = m_control->textCursor();
    cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_control->setTextCursor(cursor);
    if (m_control->find(needle, flags)) {
        showStatusMessage(tr("Search wrapped around the document"));
        return true;
    }
    m_control->setTextCursor(restore);
    showStatusMessage(tr("Not found: %1").arg(needle));
    return false;
}

bool WriteHost::findNext(const QString &needle, bool caseSensitive)
{
    return findIn(needle, caseSensitive, false);
}

bool WriteHost::findPrevious(const QString &needle, bool caseSensitive)
{
    return findIn(needle, caseSensitive, true);
}

void WriteHost::replaceCurrent(const QString &needle, const QString &replacement, bool caseSensitive)
{
    QTextCursor cursor = m_control->textCursor();
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(needle, cs) == 0) {
        cursor.insertText(replacement);
        m_document->setDirty(true);
    }
    findIn(needle, caseSensitive, false);
}

int WriteHost::replaceAll(const QString &needle, const QString &replacement, bool caseSensitive)
{
    if (needle.isEmpty()) {
        return 0;
    }
    QTextDocument *doc = m_document->textDocument();
    QTextDocument::FindFlags flags;
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    int count = 0;
    // AGENT-GUARD: the block cursor must stay VALID — doc->find() returning a
    // null cursor ended the loop and endEditBlock() ran on the null cursor,
    // a Qt no-op that left the document's editBlock counter stuck at 1 and
    // permanently broke undo grouping after the first Replace All.
    QTextCursor blockCursor(doc);
    blockCursor.beginEditBlock();
    QTextCursor cursor = doc->find(needle, QTextCursor(doc), flags);
    while (!cursor.isNull()) {
        cursor.insertText(replacement);
        ++count;
        cursor = doc->find(needle, cursor, flags);
    }
    blockCursor.endEditBlock();
    if (count > 0) {
        m_document->setDirty(true);
    }
    showStatusMessage(tr("%n replacement(s) made", nullptr, count));
    return count;
}

// ---- dialog answers -----------------------------------------------------------------

QStringList WriteHost::symbols() const
{
    return m_typography->symbols();
}

void WriteHost::insertTable(int rows, int columns)
{
    m_format->insertTableAtCursor(rows, columns);
}

void WriteHost::insertColumns(int columns, int gutterPx)
{
    m_format->insertColumnsAtCursor(columns, gutterPx);
}

void WriteHost::insertImageFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!m_format->insertImageFile(path)) {
        showStatusMessage(tr("Cannot read %1 as an image").arg(path), 8000);
    }
}

void WriteHost::insertSymbol(const QString &text)
{
    m_typography->insertSymbol(text);
}

void WriteHost::insertHyperlink(const QString &url, const QString &label)
{
    m_typography->insertHyperlink(url, label);
}

void WriteHost::commitFootnote(const QString &id, const QString &text)
{
    // One undo step with the note's text (M77: WriteDocument::setNoteTexts).
    m_notes->commitNote(QStringLiteral("footnote"), id, text);
}

void WriteHost::exportPdfTo(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (exportDocumentPdf(m_document->textDocument(), path, &error, m_document->decoration())) {
        showStatusMessage(tr("Exported %1").arg(path), 5000);
    } else {
        showStatusMessage(tr("Export failed: %1").arg(error), 8000);
    }
}

// ---- ruler --------------------------------------------------------------------------

namespace {

// The ruler's page (M88): the caret's section's, and where that page's left
// edge is in the page view (pages of other widths are centred).
struct RulerPage {
    qreal margin = 96;      // the text column's left edge (margin + gutter)
    qreal rightMargin = 96;
    qreal width = 816;
    qreal left = 0;         // in the page item, unzoomed
};

RulerPage rulerPage(const WriteDocument *document, const PageTextControl *control, const PageTextItem *page)
{
    const SectionLayout &layout = document->sections();
    const QTextCursor cursor = control->textCursor();
    const SectionGeometry &g = layout.sections().at(layout.sectionAt(cursor.position()));
    RulerPage out{g.left, g.right, g.pageSize().width(), 0};
    if (page != nullptr && control->document() == document->textDocument()) {
        const int index = page->geometry().pageForDocY(control->cursorRect().center().y());
        out.left = page->geometry().pageRect(index, page->width()).x();
    }
    return out;
}

} // namespace

QVariantMap WriteHost::rulerMarkers() const
{
    const QTextBlockFormat format = m_control->textCursor().blockFormat();
    const RulerPage page = rulerPage(m_document, m_control, m_page);
    const qreal left = page.margin + format.leftMargin();
    return {{QStringLiteral("margin"), page.margin},
            {QStringLiteral("rightMargin"), page.rightMargin},
            {QStringLiteral("pageWidth"), page.width},
            {QStringLiteral("pageLeft"), page.left},
            {QStringLiteral("left"), left},
            {QStringLiteral("firstLine"), left + format.textIndent()},
            // The paragraph's own right indent, not its section's pad (M88).
            {QStringLiteral("right"), page.width - page.rightMargin - Docx::ownRightMargin(format)}};
}

void WriteHost::applyRulerMarker(const QString &id, qreal rawDocX)
{
    const RulerPage page = rulerPage(m_document, m_control, m_page);
    const qreal margin = page.margin;
    const qreal rightMargin = page.rightMargin;
    const qreal pageWidth = page.width;
    const qreal docX = qBound(margin, rawDocX, pageWidth - rightMargin);
    if (id != QLatin1String("indent-left") && id != QLatin1String("indent-first")
        && id != QLatin1String("indent-right")) {
        return;
    }
    // Each selected paragraph gets the marker's position; the rest of its
    // format stays its own (merging the caret paragraph's whole format
    // used to copy its tab stops and Word markup onto the others).
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    for (const QTextBlock &block : selectedBlocks(cursor)) {
        QTextBlockFormat format = block.blockFormat();
        if (id == QLatin1String("indent-left")) {
            // Dragging the left indent carries the first line with it, the way
            // every word processor does — otherwise the hanging indent changes
            // every time the paragraph moves.
            format.setLeftMargin(qMax(0.0, docX - margin));
        } else if (id == QLatin1String("indent-first")) {
            format.setTextIndent(docX - margin - format.leftMargin());
        } else {
            Docx::setOwnRightMargin(format, qMax(0.0, pageWidth - rightMargin - docX)); // the pad stays (M88)
        }
        if (format.hasProperty(Docx::kTabStops)) {
            // Word's tab stops stay where they are on the page; Qt measures
            // them from the indent that just moved (docx_paragraph_format.h).
            Docx::setTabStops(format, Docx::tabStops(format));
        }
        QTextCursor(block).setBlockFormat(format);
    }
    cursor.endEditBlock();
    m_document->textDocument()->setModified(true);
    emit rulerChanged();
}

// ---- templates ----------------------------------------------------------------------

void WriteHost::loadTemplate(const QString &path)
{
    // Loading a template replaces the document: ask first when the current
    // one has unsaved changes (same contract as closing).
    if (m_document->isDirty()) {
        m_pendingTemplate = path;
        emit templateConsentRequested(path);
        return;
    }
    loadTemplateResolved(path, QStringLiteral("discard"));
}

void WriteHost::loadTemplateResolved(const QString &path, const QString &choice)
{
    m_pendingTemplate.clear();
    if (choice == QLatin1String("cancel")) {
        return;
    }
    if (choice == QLatin1String("save")) {
        // A cancelled or failed save also cancels the template load.
        m_pendingTemplate = path;
        auto *once = new QObject(this);
        connect(this, &Quick::DocumentHost::saveFinished, once, [this, once, path](bool ok) {
            once->deleteLater();
            if (ok && m_pendingTemplate == path) {
                loadTemplateResolved(path, QStringLiteral("discard"));
            } else {
                m_pendingTemplate.clear();
            }
        });
        save();
        return;
    }
    QString error;
    // The item renders the document's shared QTextDocument, so a load
    // refreshes it directly; detach so saving never touches the template.
    if (!m_document->load(path, &error)) {
        showStatusMessage(tr("Template failed: %1").arg(error), 8000);
        return;
    }
    m_document->adoptPath(QString());
    m_document->setDirty(true);
    documentReplaced();
}

} // namespace QindaOffice::Apps::Write
