// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "docx_text.h"
#include "page_geometry.h"
#include "write_footnotes.h"
#include "write_pagination.h"

#include <QTextCursor>
#include <QTextFrame>

namespace QindaOffice::Apps::Write {

WriteDocument::WriteDocument()
    : m_text(std::make_unique<QTextDocument>())
    , m_keepRules(std::make_unique<KeepRules>(m_text.get()))
{
    // A4 with QindaWrite's historical 1-inch (96 px) margins.
    setupPaged(QSizeF(PageGeometry::kDefaultPageW, PageGeometry::kDefaultPageH), 96);
    // AGENT-NOTE (Qt 6.11 quirk): QTextDocument::setPageSize() records an
    // undo item, so every fresh document was born with one phantom undo
    // step - Undo looked enabled on a pristine document and triggering it
    // reset the page size. Page geometry is setup, not user content.
    m_text->clearUndoRedoStacks();
    m_text->setModified(false);
    // AGENT-GUARD: dirty follows QTextDocument's OWN modified flag, not
    // contentsChange. contentsChange also fires for FORMAT-only edits, and
    // the spell checker is a QSyntaxHighlighter whose first pass runs while
    // the window is still being built — so every freshly created QindaWrite
    // window was born dirty. A dirty untitled window is not pristine, so
    // File ▸ Open never reused it and always spawned a second window,
    // leaving the blank one behind (ADR-0110 pristine reuse).
    connect(m_text.get(), &QTextDocument::modificationChanged, this,
            [this](bool modified) { setDirty(modified); });
    // The recovery journal still wants every real content edit; a
    // format-only pass reports zero characters added and removed.
    connect(m_text.get(), &QTextDocument::contentsChange, this,
            [this](int, int charsRemoved, int charsAdded) {
                if (charsRemoved != 0 || charsAdded != 0) {
                    emit contentChanged();
                }
            });
}

WriteDocument::~WriteDocument() = default;

void WriteDocument::applyLayout()
{
    const Docx::PageLayout &l = m_parts.layout;
    m_text->setUseDesignMetrics(true);
    m_text->setPageSize(l.pageSize);
    // AGENT-NOTE: QTextDocument paginates with the ROOT FRAME's four
    // margins; the gutter is binding space on the left, as Word lays out a
    // single-sided document. Equal margins go through setDocumentMargin:
    // Qt's HTML writer wraps a root frame whose margins differ from the
    // document margin in a "-qt-table-type: root" table, which changed what
    // an HTML round trip of an ordinary document read back.
    const qreal left = l.margins.left() + l.gutter;
    // Word's rule: a header taller than the room above the top margin
    // pushes the body down (and a tall footer pushes it up), instead of
    // overlapping it. The model keeps the document's own margins.
    qreal top = l.margins.top();
    qreal bottom = l.margins.bottom();
    for (int s = 0; s < Docx::kHeaderFooterSlots; ++s) {
        if (const auto &h = m_parts.headers[size_t(s)]; h.document) {
            top = qMax(top, l.headerDistance + Docx::headerFooterHeight(*h.document, l.contentWidth()));
        }
        if (const auto &f = m_parts.footers[size_t(s)]; f.document) {
            bottom = qMax(bottom, l.footerDistance + Docx::headerFooterHeight(*f.document, l.contentWidth()));
        }
    }
    if (qFuzzyCompare(left, l.margins.right()) && qFuzzyCompare(left, top) && qFuzzyCompare(left, bottom)) {
        m_text->setDocumentMargin(left);
    } else {
        QTextFrameFormat root = m_text->rootFrame()->frameFormat();
        root.setLeftMargin(left);
        root.setRightMargin(l.margins.right());
        root.setTopMargin(top);
        root.setBottomMargin(bottom);
        m_text->rootFrame()->setFrameFormat(root);
    }
    m_keepRules->schedule(0); // new page height, new breaks
    emit pageSetupChanged();
}

void WriteDocument::setPageLayout(const Docx::PageLayout &layout)
{
    if (layout == m_parts.layout && m_text->pageSize() == layout.pageSize) {
        return;
    }
    m_parts.layout = layout;
    applyLayout();
    m_text->setModified(true);
}

void WriteDocument::setupPaged(const QSizeF &pageSize, qreal margin)
{
    m_parts.layout.pageSize = pageSize;
    m_parts.layout.margins = QMarginsF(margin, margin, margin, margin);
    // Headers/footers sit half-way into the margin, as M14 drew them.
    m_parts.layout.headerDistance = margin / 2;
    m_parts.layout.footerDistance = margin / 2;
    m_parts.layout.gutter = 0;
    applyLayout();
}

QSizeF WriteDocument::pageSize() const
{
    return m_text->pageSize();
}

qreal WriteDocument::pageMargin() const
{
    return m_parts.layout.margins.left() + m_parts.layout.gutter;
}

// ---- headers and footers -------------------------------------------------------------

namespace {

QFont headerFont(const QTextDocument &body)
{
    QFont font = body.defaultFont();
    font.setPointSizeF(9);
    return font;
}

} // namespace

QString WriteDocument::headerText() const
{
    const Docx::HeaderFooter &h = m_parts.header(Docx::HeaderFooterSlot::Default);
    return h.document ? Docx::patternFromHeaderFooter(*h.document) : QString();
}

QString WriteDocument::footerText() const
{
    const Docx::HeaderFooter &f = m_parts.footer(Docx::HeaderFooterSlot::Default);
    return f.document ? Docx::patternFromHeaderFooter(*f.document) : QString();
}

bool WriteDocument::headerIsSimple() const
{
    bool simple = true;
    if (const auto &h = m_parts.header(Docx::HeaderFooterSlot::Default); h.document) {
        static_cast<void>(Docx::patternFromHeaderFooter(*h.document, &simple));
    }
    return simple;
}

bool WriteDocument::footerIsSimple() const
{
    bool simple = true;
    if (const auto &f = m_parts.footer(Docx::HeaderFooterSlot::Default); f.document) {
        static_cast<void>(Docx::patternFromHeaderFooter(*f.document, &simple));
    }
    return simple;
}

void WriteDocument::setHeaderFooter(const QString &header, const QString &footer)
{
    bool changed = false;
    auto apply = [&](Docx::HeaderFooter &slot, const QString &current, const QString &pattern) {
        if (current == pattern) {
            return; // an unchanged pattern keeps a rich header intact
        }
        changed = true;
        if (pattern.isEmpty()) {
            slot.document.reset();
            slot.sourcePart.clear();
            return;
        }
        slot.document = Docx::headerFooterFromPattern(pattern, headerFont(*m_text));
        slot.sourcePart.clear();
    };
    apply(m_parts.header(Docx::HeaderFooterSlot::Default), headerText(), header);
    apply(m_parts.footer(Docx::HeaderFooterSlot::Default), footerText(), footer);
    if (!changed) {
        return;
    }
    applyLayout(); // a taller header moves the body (Word's rule)
    m_text->setModified(true);
    setDirty(true);
    emit contentChanged();
}

void WriteDocument::setDifferentFirstPage(bool different)
{
    if (m_parts.differentFirstPage == different) {
        return;
    }
    m_parts.differentFirstPage = different;
    m_text->setModified(true);
    emit pageSetupChanged();
}

// ---- footnotes -------------------------------------------------------------------------

void WriteDocument::setFootnotes(QHash<QString, QString> notes)
{
    m_parts.footnotes = std::move(notes);
}

void WriteDocument::setFootnoteText(const QString &id, const QString &text)
{
    if (id.isEmpty()) {
        return;
    }
    if (text.isEmpty()) {
        m_parts.footnotes.remove(id);
    } else {
        m_parts.footnotes.insert(id, text);
    }
    m_text->setModified(true);
    emit contentChanged();
}

// ---- pictures and decoration -------------------------------------------------------

QString WriteDocument::addPicture(const QByteArray &bytes)
{
    return Docx::addPicture(m_text.get(), &m_parts.media, bytes);
}

PageDecoration WriteDocument::decoration() const
{
    PageDecoration out;
    out.layout = m_parts.layout;
    for (int s = 0; s < Docx::kHeaderFooterSlots; ++s) {
        out.headers[size_t(s)] = m_parts.headers[size_t(s)].document.get();
        out.footers[size_t(s)] = m_parts.footers[size_t(s)].document.get();
    }
    out.differentFirstPage = m_parts.differentFirstPage;
    out.differentOddEven = m_parts.differentOddEven;
    if (!m_parts.footnotes.isEmpty()) {
        // The page lookup uses the document's OWN page size, which is the
        // same size PageGeometry paints with — zoom does not affect which
        // page a position falls on.
        PageGeometry geometry;
        geometry.setPageSize(m_text->pageSize());
        out.footnotes = footnotesByPage(m_text.get(), geometry, m_parts.footnotes);
    }
    return out;
}

void WriteDocument::adoptParts(Docx::WordParts parts)
{
    m_parts = std::move(parts);
    applyLayout();
    // Word row heights need the final column widths: after the layout.
    Docx::applyRowHeights(m_text.get());
}

} // namespace QindaOffice::Apps::Write
