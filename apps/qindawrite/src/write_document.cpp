// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "docx_style_edit.h"
#include "docx_text.h"
#include "docx_math.h"
#include "page_geometry.h"
#include "docx_float.h"
#include "write_endnotes.h"
#include "write_footnotes.h"
#include "write_pagination.h"
#include "write_sections.h"

#include <QAbstractTextDocumentLayout>
#include <QAbstractUndoItem>
#include <QTextCursor>
#include <QTextFrame>

namespace QindaOffice::Apps::Write {

WriteDocument::WriteDocument()
    : m_text([] { auto text = std::make_unique<QTextDocument>(); Docx::installMathLayout(text.get()); return text; }())
    , m_sections(std::make_unique<SectionLayout>(m_text.get(), &m_parts))
    , m_keepRules(std::make_unique<KeepRules>(m_text.get()))
{
    m_keepRules->setSections(m_sections.get(), [this] { refreshSections(); });
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
        m_pageFramesValid = false;
        m_sectionsStale = true;
        // A section break typed away, pasted, undone or redone: the canvas
        // page size follows (no undo item), once the edit is through.
        if (!m_syncQueued) {
            m_syncQueued = true;
            QMetaObject::invokeMethod(this, &WriteDocument::syncCanvas, Qt::QueuedConnection);
        }
    });
    // Endnotes follow the body's last line (M77): a new layout moves them,
    // and the pages with them (M88).
    connect(m_text->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] {
        m_endnoteAreaValid = false;
        m_pageFramesValid = false;
    });
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
    // The canvas the sections need (write_sections.h): the closing
    // section's page and margins while the document has one page size.
    m_sections->update();
    m_sectionsStale = false;
    m_text->setUseDesignMetrics(true);
    // AGENT-NOTE: QTextDocument paginates with the ROOT FRAME's four
    // margins; the gutter is binding space on the left, as Word lays out a
    // single-sided document. Equal margins go through setDocumentMargin:
    // Qt's HTML writer wraps a root frame whose margins differ from the
    // document margin in a "-qt-table-type: root" table, which changed what
    // an HTML round trip of an ordinary document read back. Word's rule —
    // a tall header pushes the body down, a tall footer up — is in the
    // margins (SectionGeometry::top / bottom); the model keeps its own.
    const QMarginsF c = m_sections->canvasMargins();
    auto matches = [&c](const QTextFrameFormat &f) {
        return qFuzzyCompare(f.leftMargin(), c.left()) && qFuzzyCompare(f.rightMargin(), c.right())
            && qFuzzyCompare(f.topMargin(), c.top()) && qFuzzyCompare(f.bottomMargin(), c.bottom());
    };
    if (!matches(m_text->rootFrame()->frameFormat())) {
        if (qFuzzyCompare(c.left(), c.right()) && qFuzzyCompare(c.left(), c.top())
            && qFuzzyCompare(c.left(), c.bottom())) {
            m_text->setDocumentMargin(c.left());
        }
        // setDocumentMargin does nothing when the document margin is that
        // already (the four margins may still differ from an earlier page).
        if (QTextFrameFormat root = m_text->rootFrame()->frameFormat(); !matches(root)) {
            root.setLeftMargin(c.left());
            root.setRightMargin(c.right());
            root.setTopMargin(c.top());
            root.setBottomMargin(c.bottom());
            m_text->rootFrame()->setFrameFormat(root);
        }
    }
    // After the margins: a load reads with no page size (LayoutPause,
    // write_document_io.cpp), so this is the one layout it costs.
    m_text->setPageSize(m_sections->canvasSize());
    m_endnoteAreaValid = false;
    m_pageFramesValid = false;
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

namespace {

// The story `section` shows in its default slot, kept by it or by the
// section it links to (-1: the closing section).
const Docx::HeaderFooter *defaultStory(const Docx::WordParts &parts, const SectionLayout &sections, int section,
                                       bool footer)
{
    const int index = section < 0 ? sections.count() - 1 : qMin(section, sections.count() - 1);
    const int owner = Docx::storyOwner(parts, sections.refs(), index, footer, Docx::HeaderFooterSlot::Default);
    return owner < 0 ? nullptr
                     : Docx::sectionStory(parts, sections.refs().at(owner).key, footer, Docx::HeaderFooterSlot::Default);
}

} // namespace

QString WriteDocument::headerText(int section) const
{
    refreshSections();
    const Docx::HeaderFooter *h = defaultStory(m_parts, *m_sections, section, false);
    return h != nullptr && h->document ? Docx::patternFromHeaderFooter(*h->document) : QString();
}

QString WriteDocument::footerText(int section) const
{
    refreshSections();
    const Docx::HeaderFooter *f = defaultStory(m_parts, *m_sections, section, true);
    return f != nullptr && f->document ? Docx::patternFromHeaderFooter(*f->document) : QString();
}

bool WriteDocument::headerIsSimple(int section) const
{
    refreshSections();
    bool simple = true;
    if (const Docx::HeaderFooter *h = defaultStory(m_parts, *m_sections, section, false); h && h->document) {
        static_cast<void>(Docx::patternFromHeaderFooter(*h->document, &simple));
    }
    return simple;
}

bool WriteDocument::footerIsSimple(int section) const
{
    refreshSections();
    bool simple = true;
    if (const Docx::HeaderFooter *f = defaultStory(m_parts, *m_sections, section, true); f && f->document) {
        static_cast<void>(Docx::patternFromHeaderFooter(*f->document, &simple));
    }
    return simple;
}

void WriteDocument::setHeaderFooter(const QString &header, const QString &footer, int section)
{
    refreshSections();
    const int index = section < 0 ? m_sections->count() - 1 : qMin(section, m_sections->count() - 1);
    const QString oldHeader = headerText(index);
    const QString oldFooter = footerText(index);
    if (header != oldHeader || footer != oldFooter) {
        emit storiesAboutToBeReplaced();
    }
    bool changed = false;
    auto apply = [&](bool isFooter, const QString &current, const QString &pattern) {
        if (current == pattern) {
            return; // an unchanged pattern keeps a rich header intact
        }
        changed = true;
        // The story the section shows: its own, the one it links to, or
        // (none yet) a new one in the first section that would show it.
        const int keeper = storyKeeper(index, isFooter, Docx::HeaderFooterSlot::Default);
        Docx::HeaderFooter &slot = *Docx::sectionStory(m_parts, m_sections->refs().at(keeper).key, isFooter,
                                                       Docx::HeaderFooterSlot::Default, true);
        if (pattern.isEmpty()) {
            slot.document.reset();
            slot.sourcePart.clear();
            return;
        }
        slot.document = Docx::headerFooterFromPattern(pattern, headerFont(*m_text));
        slot.sourcePart.clear();
    };
    apply(false, oldHeader, header);
    apply(true, oldFooter, footer);
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
    m_sectionsStale = true; // the closing section's settings changed (M88)
    m_pageFramesValid = false;
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
    // Every page's own frame (M88): its section's size, margins, header,
    // footer and number.
    out.pages = pageFrames();
    if (!m_parts.footnotes.isEmpty()) {
        // The page lookup uses the document's OWN (canvas) page size, which
        // is the same size PageGeometry paginates with — zoom does not
        // affect which page a position falls on.
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
    // As SectionLayout::pageFrames picks what a page shows (M88).
    const PageFrame frame = pageFrame(page - 1);
    const Docx::SectionSettings &settings = sections().sections().at(frame.section).ref.settings;
    if (settings.titlePage && frame.firstOfSection) {
        return Docx::HeaderFooterSlot::First;
    }
    if (m_parts.differentOddEven && frame.number % 2 == 0
        && m_sections->story(frame.section, footer, Docx::HeaderFooterSlot::Even) != nullptr) {
        return Docx::HeaderFooterSlot::Even;
    }
    return Docx::HeaderFooterSlot::Default;
}

QTextDocument *WriteDocument::storyDocument(bool footer, Docx::HeaderFooterSlot slot, bool create)
{
    refreshSections();
    return storyDocument(m_sections->count() - 1, footer, slot, create);
}

QTextDocument *WriteDocument::storyDocument(int section, bool footer, Docx::HeaderFooterSlot slot, bool create)
{
    refreshSections();
    const int index = qBound(0, section, m_sections->count() - 1);
    Docx::HeaderFooter *story = Docx::sectionStory(m_parts, m_sections->refs().at(index).key, footer, slot, create);
    if (story == nullptr) {
        return nullptr;
    }
    if (!story->document && create) {
        story->document = newStory();
    }
    return story->document.get();
}

std::unique_ptr<QTextDocument> WriteDocument::newStory() const
{
    auto story = std::make_unique<QTextDocument>();
    Docx::installMathLayout(story.get());
    story->setDefaultFont(headerFont(*m_text));
    story->setDocumentMargin(0);
    story->clearUndoRedoStacks();
    story->setModified(false); // empty: nothing to save yet
    return story;
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
