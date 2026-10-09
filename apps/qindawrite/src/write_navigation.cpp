// SPDX-License-Identifier: GPL-3.0-or-later
// NavigationModel (write_navigation.h): the Navigation pane's headings,
// going to one, collapsing, and moving a heading with its section.

#include "write_navigation.h"

#include "docx_list_labels.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextTable>

#include <tuple>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

NavigationModel::NavigationModel(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                 QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_pane = model->add(QStringLiteral("writeNavigationPaneAction"), tr("&Navigation Pane"), {}, QStringLiteral("navigation"));
    m_pane->setCheckable(true);
    connect(m_pane, &ActionEntry::toggled, this, &NavigationModel::paneToggleRequested);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(300);
    connect(&m_refresh, &QTimer::timeout, this, &NavigationModel::refresh);
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &NavigationModel::syncCurrent);
    refresh();
}

QList<MenuNode> NavigationModel::menuItems() const
{
    return {MenuNode::action(m_pane->id())};
}

void NavigationModel::refresh()
{
    m_headings.clear();
    const QTextDocument &body = *m_document->textDocument();
    for (QTextBlock block = body.begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat f = block.blockFormat();
        const int outline = Docx::outlineLevel(f);
        const QString text = block.text().simplified();
        if (outline < 0 || outline > 8 || text.isEmpty() || f.boolProperty(Docx::kParagraphSynthetic)
            || f.boolProperty(Docx::kWriteTocBlockProperty)) {
            continue;
        }
        m_headings.append({block.position(), outline + 1, text, Docx::listMarker(block).text});
    }
    m_rows.clear();
    int hiddenBelow = 10; // headings deeper than this are under a collapsed one
    for (int i = 0; i < m_headings.size(); ++i) {
        const Heading &h = m_headings.at(i);
        if (h.level <= hiddenBelow) {
            hiddenBelow = 10;
        }
        const bool visible = hiddenBelow == 10;
        const bool expandable = i + 1 < m_headings.size() && m_headings.at(i + 1).level > h.level;
        const bool expanded = !m_collapsed.contains(QStringLiteral("%1|%2").arg(h.level).arg(h.text));
        if (visible && expandable && !expanded) {
            hiddenBelow = h.level;
        }
        m_rows.append(QVariantMap{{QStringLiteral("index"), i},
                                  {QStringLiteral("level"), h.level},
                                  {QStringLiteral("text"), h.text},
                                  {QStringLiteral("number"), h.number},
                                  {QStringLiteral("expandable"), expandable},
                                  {QStringLiteral("expanded"), expanded},
                                  {QStringLiteral("visible"), visible}});
    }
    emit headingsChanged();
    syncCurrent();
}

void NavigationModel::syncCurrent()
{
    const int position = m_control->document() == m_document->textDocument() ? m_control->textCursor().position() : -1;
    int current = -1;
    for (int i = 0; i < m_headings.size() && m_headings.at(i).position <= position; ++i) {
        current = i;
    }
    if (current != m_current) {
        m_current = current;
        emit currentChanged();
    }
}

void NavigationModel::go(int index)
{
    if (index < 0 || index >= m_headings.size() || m_control->document() != m_document->textDocument()) {
        return;
    }
    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(qMin(m_headings.at(index).position, m_document->textDocument()->characterCount() - 1));
    m_control->setTextCursor(cursor);
}

void NavigationModel::toggle(int index)
{
    if (index < 0 || index >= m_headings.size()) {
        return;
    }
    const QString key = QStringLiteral("%1|%2").arg(m_headings.at(index).level).arg(m_headings.at(index).text);
    if (!m_collapsed.remove(key)) {
        m_collapsed.insert(key);
    }
    refresh();
}

int NavigationModel::sectionEnd(int index) const
{
    for (int j = index + 1; j < m_headings.size(); ++j) {
        if (m_headings.at(j).level <= m_headings.at(index).level) {
            return m_headings.at(j).position;
        }
    }
    return -1; // the end of the body
}

bool NavigationModel::move(int index, bool up)
{
    if (index < 0 || index >= m_headings.size() || m_control->document() != m_document->textDocument()) {
        return false;
    }
    // Moving a section down is moving the next sibling section up.
    const int level = m_headings.at(index).level;
    int moving = index;
    int before = -1;
    if (up) {
        for (int j = index - 1; j >= 0 && m_headings.at(j).level >= level; --j) {
            if (m_headings.at(j).level == level) {
                before = j;
                break;
            }
        }
    } else {
        const int end = sectionEnd(index);
        for (int j = index + 1; j < m_headings.size() && end >= 0; ++j) {
            if (m_headings.at(j).position == end && m_headings.at(j).level == level) {
                moving = j;
                before = index;
            }
        }
    }
    QTextDocument *document = m_document->textDocument();
    const auto inTable = [document](int position) {
        QTextCursor at(document);
        at.setPosition(position);
        return at.currentTable() != nullptr;
    };
    if (before < 0 || inTable(m_headings.at(moving).position) || inTable(m_headings.at(before).position)) {
        return false; // a heading in a table cell has no section of its own to move
    }
    int after = moving + 1; // the moving section's headings: [moving, after)
    while (after < m_headings.size() && m_headings.at(after).level > level) {
        ++after;
    }
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    int end = sectionEnd(moving);
    const bool last = end < 0;
    if (last) {
        // A last section ends without a paragraph mark to carry: lend it one.
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        end = cursor.block().position();
    }
    // AGENT-NOTE: a fragment inserted at a paragraph's start merges its
    // first paragraph into that one and hands the paragraph it lands in
    // its own last (empty) paragraph's format; both paragraphs get their
    // own formats back — style, list membership, Word markup.
    const QTextBlock movingBlock = document->findBlock(m_headings.at(moving).position);
    const QTextBlock targetBlock = document->findBlock(m_headings.at(before).position);
    const QTextBlockFormat movingFormat = movingBlock.blockFormat();
    const QTextCharFormat movingMark = movingBlock.charFormat();
    const QTextBlockFormat targetFormat = targetBlock.blockFormat();
    const QTextCharFormat targetMark = targetBlock.charFormat();
    QTextCursor section(document);
    section.setPosition(m_headings.at(moving).position);
    section.setPosition(end, QTextCursor::KeepAnchor);
    const QTextDocumentFragment fragment = section.selection();
    section.removeSelectedText();
    QTextCursor into(document);
    const int at = m_headings.at(before).position;
    into.setPosition(at);
    into.insertFragment(fragment);
    for (const auto &[position, block, mark] : {std::tuple{at, movingFormat, movingMark},
                                                std::tuple{into.position(), targetFormat, targetMark}}) {
        QTextCursor fix(document);
        fix.setPosition(position);
        fix.setBlockFormat(block);
        fix.setBlockCharFormat(mark);
    }
    if (last) {
        QTextCursor tail(document);
        tail.movePosition(QTextCursor::End);
        tail.deletePreviousChar(); // the lent paragraph mark
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
    refresh();
    go(up ? before : index + (after - moving));
    return true;
}

} // namespace QindaOffice::Apps::Write
