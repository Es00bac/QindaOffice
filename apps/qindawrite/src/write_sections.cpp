// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_sections.h"

#include "docx_properties.h"
#include "docx_text.h"

#include <QAbstractTextDocumentLayout>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <cmath>

namespace QindaOffice::Apps::Write {

namespace {

constexpr qreal kSame = 0.01;

// The canvas pages the first and last lines of `block` fall on; the
// block's box starts where its layout did, a pushed line has its own y
// (tst_write_probe_sections).
std::pair<int, int> linePages(const QTextBlock &block, QAbstractTextDocumentLayout *layout, qreal pageHeight)
{
    const QTextLayout *lines = block.isValid() ? block.layout() : nullptr;
    if (lines == nullptr || lines->lineCount() == 0) {
        return {-1, -1};
    }
    const qreal origin = layout->blockBoundingRect(block).top();
    auto page = [&](const QTextLine &line) {
        return int(std::floor((origin + line.y() + line.height() / 2) / pageHeight));
    };
    return {page(lines->lineAt(0)), page(lines->lineAt(lines->lineCount() - 1))};
}

QSet<int> endNumbers(const QList<Docx::SectionRef> &refs)
{
    QSet<int> out;
    for (const Docx::SectionRef &ref : refs) {
        if (!ref.closing()) {
            out.insert(ref.end.blockNumber());
        }
    }
    return out;
}

// The pad a format has applied now (0 when a command set the margin since).
qreal appliedPad(const QTextFormat &format)
{
    const QVariantList pad = format.property(Docx::kSectionPad).toList();
    if (pad.size() != 2) {
        return 0;
    }
    const qreal margin = format.isFrameFormat() ? format.toFrameFormat().rightMargin()
                                                : format.doubleProperty(QTextFormat::BlockRightMargin);
    return qAbs(margin - pad.at(1).toReal()) < kSame ? pad.at(0).toReal() : -1;
}

bool padDiffers(const QTextFormat &format, qreal pad)
{
    const qreal now = appliedPad(format);
    if (pad <= kSame) {
        return format.hasProperty(Docx::kSectionPad);
    }
    return now < 0 || qAbs(now - pad) > kSame;
}

} // namespace

bool SectionGeometry::startsPage(int index) const
{
    return index > 0 && ref.settings.flow.start != Docx::SectionStart::Continuous;
}

SectionLayout::SectionLayout(QTextDocument *body, Docx::WordParts *parts)
    : m_body(body)
    , m_parts(parts)
{
    update();
}

void SectionLayout::update()
{
    m_refs = Docx::documentSections(*m_body, *m_parts);
    m_sections.clear();
    for (qsizetype i = 0; i < m_refs.size(); ++i) {
        SectionGeometry g;
        g.ref = m_refs.at(i);
        const Docx::PageLayout &l = g.ref.settings.layout;
        g.left = l.margins.left() + l.gutter;
        g.right = l.margins.right();
        // Word's rule: a header taller than the room above the top margin
        // pushes the body down (a tall footer pushes it up), instead of
        // overlapping it. The section keeps its own margins.
        const qreal width = qMax<qreal>(48, l.contentWidth());
        g.top = qMax(l.margins.top(), l.headerDistance + storyHeight(int(i), false, width));
        g.bottom = qMax(l.margins.bottom(), l.footerDistance + storyHeight(int(i), true, width));
        m_sections.append(g);
    }
    // AGENT-NOTE: the canvas's margins are the closing section's, so a
    // document of one section is laid out on its own page, as before M88.
    const SectionGeometry &closing = m_sections.constLast();
    qreal width = 0;
    qreal height = 0;
    for (const SectionGeometry &g : std::as_const(m_sections)) {
        width = qMax(width, g.textWidth());
        height = qMax(height, g.textHeight());
    }
    m_canvasMargins = QMarginsF(closing.left, closing.top, closing.right, closing.bottom);
    m_canvasSize = QSizeF(closing.left + qMax<qreal>(48, width) + closing.right,
                          closing.top + qMax<qreal>(48, height) + closing.bottom);
}

int SectionLayout::sectionAt(int position) const
{
    return Docx::sectionIndexAt(m_refs, position);
}

qreal SectionLayout::canvasTextWidth() const
{
    return m_canvasSize.width() - m_canvasMargins.left() - m_canvasMargins.right();
}

qreal SectionLayout::canvasTextHeight() const
{
    return m_canvasSize.height() - m_canvasMargins.top() - m_canvasMargins.bottom();
}

qreal SectionLayout::padFor(int index) const
{
    if (index < 0 || index >= m_sections.size()) {
        return 0;
    }
    return qMax<qreal>(0, canvasTextWidth() - qMax<qreal>(48, m_sections.at(index).textWidth()));
}

int SectionLayout::applyPads(const std::function<void(const std::function<void()> &)> &edit) const
{
    const QSet<int> ends = endNumbers(m_refs);
    int changed = 0;
    int section = 0;
    for (auto it = m_body->rootFrame()->begin(); !it.atEnd(); ++it) {
        const qreal pad = padFor(qMin(section, count() - 1));
        if (QTextFrame *frame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(frame)) {
                QTextTableFormat format = table->format();
                if (padDiffers(format, pad)) {
                    edit([&] {
                        Docx::setSectionPad(format, pad);
                        table->setFormat(format);
                    });
                    ++changed;
                }
            }
            continue;
        }
        const QTextBlock block = it.currentBlock();
        QTextBlockFormat format = block.blockFormat();
        if (padDiffers(format, pad)) {
            edit([&] {
                Docx::setSectionPad(format, pad);
                QTextCursor(block).setBlockFormat(format);
            });
            ++changed;
        }
        if (ends.contains(block.blockNumber())) {
            ++section;
        }
    }
    return changed;
}

const QTextDocument *SectionLayout::story(int index, bool footer, Docx::HeaderFooterSlot slot) const
{
    const int owner = Docx::storyOwner(*m_parts, m_refs, index, footer, slot);
    if (owner < 0) {
        return nullptr;
    }
    const Docx::HeaderFooter *story = Docx::sectionStory(*m_parts, m_refs.at(owner).key, footer, slot);
    return story != nullptr ? story->document.get() : nullptr;
}

qreal SectionLayout::storyHeight(int index, bool footer, qreal width) const
{
    qreal height = 0;
    for (int s = 0; s < Docx::kHeaderFooterSlots; ++s) {
        if (const QTextDocument *document = story(index, footer, Docx::HeaderFooterSlot(s))) {
            // AGENT-NOTE: laying a header out clones it (its fields filled
            // in); the cache keeps that off every keystroke and caret move.
            Height &known = m_heights[document];
            if (known.document != document || known.revision != document->revision()
                || !qFuzzyCompare(known.width, width)) {
                known = {document, document->revision(), width, Docx::headerFooterHeight(*document, width)};
            }
            height = qMax(height, known.height);
        }
    }
    return height;
}

QList<int> SectionLayout::pageSections(int pages) const
{
    QList<int> owner(qMax(0, pages), -1);
    if (count() <= 1 || pages <= 0) {
        owner.fill(0);
        return owner;
    }
    QAbstractTextDocumentLayout *layout = m_body->documentLayout();
    const qreal height = m_body->pageSize().height();
    const QSet<int> ends = endNumbers(m_refs);
    int section = 0;
    auto assign = [&](std::pair<int, int> span) {
        for (int p = qMax(0, span.first); p >= 0 && p <= span.second && p < pages; ++p) {
            if (owner[p] < 0) {
                owner[p] = section;
            }
        }
    };
    for (auto it = m_body->rootFrame()->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *frame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(frame)) {
                const int first = linePages(table->cellAt(0, 0).firstCursorPosition().block(), layout, height).first;
                const int last = linePages(table->lastCursorPosition().block(), layout, height).second;
                assign({first, qMax(first, last)});
            }
            continue;
        }
        const QTextBlock block = it.currentBlock();
        assign(linePages(block, layout, height));
        if (ends.contains(block.blockNumber())) {
            section = qMin(section + 1, count() - 1);
        }
    }
    // A page with no text of its own (an odd or even page section start's
    // blank page, endnotes running on) belongs to the section before.
    for (int p = 0; p < pages; ++p) {
        if (owner[p] < 0) {
            owner[p] = p > 0 ? owner[p - 1] : 0;
        }
    }
    return owner;
}

QList<PageFrame> SectionLayout::pageFrames(int pages) const
{
    const QList<int> owner = pageSections(pages);
    QList<int> perSection(count(), 0);
    for (const int s : owner) {
        ++perSection[s];
    }
    const qreal canvasText = canvasTextHeight();
    QList<PageFrame> out;
    out.reserve(pages);
    int number = 0;
    for (int p = 0; p < pages; ++p) {
        const int s = owner.at(p);
        const SectionGeometry &g = m_sections.at(s);
        PageFrame frame;
        frame.section = s;
        frame.size = g.pageSize();
        // The section's text area laid over the canvas page's.
        frame.origin = QPointF(m_canvasMargins.left() - g.left,
                               p * m_canvasSize.height() + m_canvasMargins.top() - g.top);
        frame.layout = g.ref.settings.layout;
        frame.firstOfSection = p == 0 || owner.at(p - 1) != s;
        const Docx::PageNumbering &numbering = g.ref.settings.flow.numbering;
        number = frame.firstOfSection && numbering.start >= 0 ? numbering.start : number + 1;
        frame.number = number;
        frame.numberFormat = numbering.format;
        frame.sectionPages = perSection.at(s);
        // Word's slots: the section's first page, even pages (the
        // document's switch), the rest; a slot it lacks links back.
        const bool even = m_parts->differentOddEven && number % 2 == 0;
        auto pick = [&](bool footer) -> const QTextDocument * {
            if (g.ref.settings.titlePage && frame.firstOfSection) {
                return story(s, footer, Docx::HeaderFooterSlot::First);
            }
            if (even) {
                if (const QTextDocument *document = story(s, footer, Docx::HeaderFooterSlot::Even)) {
                    return document;
                }
            }
            return story(s, footer, Docx::HeaderFooterSlot::Default);
        };
        frame.header = pick(false);
        frame.footer = pick(true);
        if (g.textHeight() < canvasText - 0.5) {
            frame.textBottom = g.top + g.textHeight();
        }
        out.append(frame);
    }
    return out;
}

} // namespace QindaOffice::Apps::Write
