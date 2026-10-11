// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_story_editor.h"

#include "page_text_control.h"
#include "write_document.h"
#include "write_sections.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

// Paper colours (D-004): the band's rule and tab are drawn on the page, in
// print-like greys, like the paper edge.
const QColor kBandRule(0x8a, 0x8a, 0x96);
const QColor kTabFill(0xe6, 0xe8, 0xee);
const QColor kTabInk(0x30, 0x30, 0x3a);
constexpr qreal kMinimumBand = 18; // an empty story still has a line to click
constexpr int kSettleMs = 250;

} // namespace

StoryEditor::StoryEditor(WriteDocument *document, PageTextControl *control, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_control(control)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(kSettleMs);
    connect(m_document, &WriteDocument::storiesAboutToBeReplaced, this, &StoryEditor::close);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        if (isOpen()) {
            m_document->storyEdited();
            emit settled();
        }
    });
}

StoryEditor::~StoryEditor()
{
    close();
}

QString StoryEditor::label() const
{
    if (!isOpen()) {
        return {};
    }
    const bool footer = m_kind == Kind::Footer;
    QString text;
    switch (m_slot) {
    case Docx::HeaderFooterSlot::First:
        text = footer ? tr("First Page Footer") : tr("First Page Header");
        break;
    case Docx::HeaderFooterSlot::Even:
        text = footer ? tr("Even Page Footer") : tr("Even Page Header");
        break;
    default:
        text = footer ? tr("Footer") : tr("Header");
        break;
    }
    // Word's band tab names the section and says when it is linked (M88).
    if (m_document->sections().count() > 1) {
        text += tr(" -Section %1-").arg(m_section + 1);
    }
    if (m_keeper != m_section) {
        text += tr("  Same as Previous");
    }
    return text;
}

bool StoryEditor::open(Kind kind, int page)
{
    if (m_document == nullptr || m_control == nullptr || kind == Kind::None) {
        return false;
    }
    close();
    const bool footer = kind == Kind::Footer;
    // The page's section's slot; the story is the section's own or the one
    // it links to (M88: editing a linked header edits the previous one, as
    // in Word).
    m_section = m_document->pageFrame(page).section;
    m_slot = m_document->storySlot(page + 1, footer);
    m_keeper = m_document->storyKeeper(m_section, footer, m_slot);
    m_story = m_document->storyDocument(m_keeper, footer, m_slot, true);
    if (m_story == nullptr) {
        return false;
    }
    m_kind = kind;
    m_page = page;
    prepareStory();
    m_bodyCursor = m_control->textCursor();
    m_control->setDocument(m_story);
    connect(m_story, &QTextDocument::contentsChanged, &m_settle, qOverload<>(&QTimer::start));
    emit openChanged();
    return true;
}

void StoryEditor::close()
{
    if (!isOpen()) {
        return;
    }
    if (m_story != nullptr) {
        disconnect(m_story, nullptr, &m_settle, nullptr);
    }
    if (m_settle.isActive()) {
        m_settle.stop();
        if (m_story != nullptr) {
            m_document->storyEdited();
        }
    }
    m_control->setDocument(m_document->textDocument());
    if (!m_bodyCursor.isNull() && m_bodyCursor.document() == m_document->textDocument()) {
        m_control->setTextCursor(m_bodyCursor);
    }
    m_kind = Kind::None;
    m_story = nullptr;
    emit openChanged();
}

void StoryEditor::prepareStory()
{
    // Laid out across the text column, as the page draws it (pdf_exporter's
    // drawStory); setting the margin is a format change Qt records, so the
    // story's own modified flag and undo history are kept as they were.
    const bool modified = m_story->isModified();
    const bool undo = m_story->isUndoAvailable();
    if (!qFuzzyIsNull(m_story->documentMargin())) {
        m_story->setDocumentMargin(0);
    }
    m_story->setTextWidth(qMax<qreal>(48, m_document->pageFrame(m_page).layout.contentWidth()));
    if (!undo) {
        m_story->clearUndoRedoStacks(QTextDocument::UndoStack);
    }
    m_story->setModified(modified);
}

QRectF StoryEditor::bandRect() const
{
    if (!isOpen()) {
        return {};
    }
    // The band on its page's own section page (M88).
    const PageFrame frame = m_document->pageFrame(m_page);
    const Docx::PageLayout &l = frame.layout;
    const qreal width = qMax<qreal>(48, l.contentWidth());
    const qreal height = qMax(kMinimumBand, m_story->size().height());
    const qreal left = l.margins.left() + l.gutter;
    if (m_kind == Kind::Header) {
        return {left, l.headerDistance, width, height};
    }
    return {left, frame.size.height() - l.footerDistance - height, width, height};
}

StoryEditor::Kind StoryEditor::zoneAt(int page, const QPointF &pagePos) const
{
    const qreal height = m_document->pageFrame(page).size.height();
    if (pagePos.y() >= 0 && pagePos.y() < m_document->bodyTop(page)) {
        return Kind::Header;
    }
    if (pagePos.y() > height - m_document->bodyBottom(page) && pagePos.y() <= height) {
        return Kind::Footer;
    }
    return Kind::None;
}

QPointF StoryEditor::pageToStory(const QPointF &pagePos) const
{
    return pagePos - bandRect().topLeft();
}

QPointF StoryEditor::storyToPage(const QPointF &storyPos) const
{
    return storyPos + bandRect().topLeft();
}

void StoryEditor::drawOpenStory(QPainter *painter, const QRectF &pageRect) const
{
    if (!isOpen()) {
        return;
    }
    const QRectF band = bandRect().translated(pageRect.topLeft());
    painter->save();
    painter->fillRect(band.adjusted(-4, -4, 4, 4), m_control->paperPalette().base());
    painter->translate(band.topLeft());
    m_control->drawContents(painter, QRectF(QPointF(-4, -4), band.size() + QSizeF(8, 8)));
    painter->restore();

    painter->save();
    QPen rule(kBandRule);
    rule.setStyle(Qt::DashLine);
    painter->setPen(rule);
    const bool header = m_kind == Kind::Header;
    const qreal y = header ? band.bottom() + 4 : band.top() - 4;
    painter->drawLine(QPointF(pageRect.left(), y), QPointF(pageRect.right(), y));
    QFont font = painter->font();
    font.setPointSizeF(7.5);
    painter->setFont(font);
    const QString text = label();
    const QFontMetricsF metrics(font);
    const QSizeF size(metrics.horizontalAdvance(text) + 12, metrics.height() + 4);
    const QRectF tab(QPointF(band.left(), header ? y : y - size.height()), size);
    painter->fillRect(tab, kTabFill);
    painter->setPen(kTabInk);
    painter->drawText(tab, Qt::AlignCenter, text);
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
