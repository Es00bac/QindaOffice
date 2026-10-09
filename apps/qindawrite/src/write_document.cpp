// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "docx_style_edit.h"
#include "docx_text.h"
#include "page_geometry.h"
#include "docx_float.h"
#include "write_endnotes.h"
#include "write_footnotes.h"
#include "write_pagination.h"

#include <QAbstractTextDocumentLayout>
#include <QAbstractUndoItem>
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
    connect(m_text.get(), &QTextDocument::contentsChange, this, [this] {
        m_commentSpansValid = false;
        m_endnoteAreaValid = false;
    });
    // Endnotes follow the body's last line (M77): a new layout moves them.
    connect(m_text->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this] { m_endnoteAreaValid = false; });
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
    m_bodyTop = top;
    m_bodyBottom = bottom;
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
    if (header != headerText() || footer != footerText()) {
        emit storiesAboutToBeReplaced();
    }
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
    emit storiesAboutToBeReplaced(); // page 1 shows another slot now
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
    out.endnotes = endnoteArea();
    out.overTextPictures = m_text->documentLayout()->findChild<QObject *>(QStringLiteral("qqoPictureHandler")) != nullptr;
    return out;
}

void WriteDocument::setComments(QList<Docx::Comment> comments)
{
    m_parts.comments = std::move(comments);
    m_text->setModified(true);
    setDirty(true);
    emit commentsChanged();
}

const QList<Docx::CommentSpan> &WriteDocument::commentSpans() const
{
    if (!m_commentSpansValid) {
        m_commentSpans = m_parts.comments.isEmpty() ? QList<Docx::CommentSpan>() : Docx::commentSpans(*m_text);
        m_commentSpansValid = true;
    }
    return m_commentSpans;
}

void WriteDocument::setActiveComment(const QString &id)
{
    if (m_activeComment != id) {
        m_activeComment = id;
        emit activeCommentChanged();
    }
}

Docx::HeaderFooterSlot WriteDocument::storySlot(int page, bool footer) const
{
    const auto &stories = footer ? m_parts.footers : m_parts.headers;
    if (m_parts.differentFirstPage && page == 1) {
        return Docx::HeaderFooterSlot::First;
    }
    if (m_parts.differentOddEven && page % 2 == 0 && stories[size_t(Docx::HeaderFooterSlot::Even)].document) {
        return Docx::HeaderFooterSlot::Even;
    }
    return Docx::HeaderFooterSlot::Default;
}

QTextDocument *WriteDocument::storyDocument(bool footer, Docx::HeaderFooterSlot slot, bool create)
{
    Docx::HeaderFooter &story = footer ? m_parts.footer(slot) : m_parts.header(slot);
    if (!story.document && create) {
        story.document = std::make_unique<QTextDocument>();
        story.document->setDefaultFont(headerFont(*m_text));
        story.document->setDocumentMargin(0);
        story.document->clearUndoRedoStacks();
        story.document->setModified(false); // empty: nothing to save yet
    }
    return story.document.get();
}

void WriteDocument::storyEdited()
{
    applyLayout(); // a taller header moves the body (Word's rule)
    m_text->setModified(true);
    setDirty(true);
    emit contentChanged();
}

void WriteDocument::setDefaultTabStop(qreal px)
{
    if (px <= 0 || qAbs(px - m_parts.defaultTabStop) < 0.01) {
        return;
    }
    m_parts.defaultTabStop = px;
    QTextOption option = m_text->defaultTextOption();
    option.setTabStopDistance(px);
    m_text->setDefaultTextOption(option);
    m_text->setModified(true);
    setDirty(true);
}

void WriteDocument::adoptParts(Docx::WordParts parts)
{
    emit storiesAboutToBeReplaced();
    m_parts = std::move(parts);
    m_commentSpansValid = false;
    m_endnoteAreaValid = false;
    m_activeComment.clear();
    emit commentsChanged();
    applyLayout();
    // Word row heights need the final column widths: after the layout.
    Docx::applyRowHeights(m_text.get());
    emit stylesChanged();
}

// ---- styles (M73, D-082) --------------------------------------------------------------

namespace {

// Swaps the document's styles part back and forth with the text's own
// Undo and Redo: one item inside the style command's edit block.
class StylesUndo final : public QAbstractUndoItem {
public:
    StylesUndo(WriteDocument *document, Ooxml::XmlElementPtr before, Ooxml::XmlElementPtr after)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void undo() override { m_document->swapStyles(m_before); }
    void redo() override { m_document->swapStyles(m_after); }

private:
    WriteDocument *m_document;
    Ooxml::XmlElementPtr m_before;
    Ooxml::XmlElementPtr m_after;
};

} // namespace

Ooxml::XmlElementPtr WriteDocument::stylesRoot() const
{
    return Docx::stylesRoot(m_parts, m_text->defaultFont());
}

void WriteDocument::setStylesRoot(Ooxml::XmlElementPtr root, QTextDocument *story)
{
    Ooxml::XmlElementPtr before = m_parts.styles;
    swapStyles(root);
    (story != nullptr ? story : m_text.get())->appendUndoItem(new StylesUndo(this, std::move(before), std::move(root)));
}

void WriteDocument::swapStyles(Ooxml::XmlElementPtr root)
{
    m_parts.styles = std::move(root);
    emit stylesChanged();
}

} // namespace QindaOffice::Apps::Write
