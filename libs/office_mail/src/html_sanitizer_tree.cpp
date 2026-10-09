// SPDX-License-Identifier: GPL-3.0-or-later
// The sanitizer's open-element stack (html_sanitizer_p.h): opening and
// closing elements, the implied end tags HTML5 inserts, and table repair
// within the bounds that keep Qt's table layout affordable.
//
// AGENT-NOTE: the output is balanced by construction — an end tag is only
// ever written by popTo(), for an element openWith() wrote, and finish() pops
// whatever is left. Input end tags merely choose how far to pop; one that
// matches nothing open (in scope) is dropped.
//
// AGENT-NOTE (tables, measured with Qt 6.11): QTextDocument pads every row to
// the widest one, so 2 000 one-cell rows plus a 200-cell row (40 KB of HTML)
// became a 2 001 × 200 grid: 17 s and 270 MB. Content in nested tables is laid
// out about twice per level. Hence kMaxTableColumns, the kMaxTableCells budget
// (Σ rows × columns), maxTableDepth and flattening: a row or cell the budget
// refuses becomes a <br> or a space inside the current cell, and a table over
// the limits becomes plain flow (rows → <br>, cells → spaces). A cell stays
// open after </td> (only its content closes) so that there is always a cell
// to flatten into; the next <td>, <tr> or </table> closes it.

#include "html_sanitizer_p.h"

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail::Detail {

namespace {

bool isTableStructure(Tag tag)
{
    return tag == Tag::Table || tag == Tag::Tbody || tag == Tag::Thead || tag == Tag::Tfoot
           || tag == Tag::Tr;
}

// The innermost open element matching `match`, searching down from the top
// and giving up at an element with any of the `stop` flags; -1 if none.
template<typename Match>
int findOpen(const std::vector<OpenElement> &stack, Match match, quint16 stop)
{
    const OpenElement *open = stack.data(); // raw access keeps -O0 builds fast too
    for (int i = int(stack.size()) - 1; i >= 0; --i) {
        const OpenElement &element = open[i];
        if (match(element))
            return i;
        if (element.flags & stop)
            return -1;
    }
    return -1;
}

auto is(Tag wanted)
{
    return [wanted](const OpenElement &element) { return element.tag == wanted; };
}

// HTML5's search for the list item an <li> (or <dd>/<dt>) closes: it does
// not cross a list, a table part or another block container; div, p and
// address are crossed.
int findListItem(const std::vector<OpenElement> &stack, bool definition)
{
    const OpenElement *open = stack.data();
    for (int i = int(stack.size()) - 1; i >= 0; --i) {
        const Tag tag = open[i].tag;
        if (definition ? (tag == Tag::Dd || tag == Tag::Dt) : tag == Tag::Li)
            return i;
        const quint16 flags = open[i].flags;
        if ((flags & (Scope | ListBox))
            || ((flags & Block) && tag != Tag::Div && tag != Tag::P && tag != Tag::Address))
            return -1;
    }
    return -1;
}

int colspanOf(const HtmlToken &token)
{
    for (const HtmlAttribute &attribute : token.attributes) {
        if (attribute.name == "colspan"_L1)
            return qBound(1, leadingInteger(attribute.value), kMaxTableColumns);
    }
    return 1;
}

} // namespace

bool Sanitizer::open(Tag tag, const HtmlToken *token)
{
    if (!(tagInfo(tag).flags & Void) && m_stack.size() >= std::size_t(kMaxOpenElements)) {
        ++m_suppressed[index(tag)]; // its end tag is swallowed too (endTag())
        return false;
    }
    openWith(tag, token ? attributes(tag, *token) : QString());
    return true;
}

void Sanitizer::openWith(Tag tag, const QString &attributes)
{
    const TagInfo &info = tagInfo(tag);
    m_contentStarted = true;
    m_body += u'<';
    m_body += QLatin1StringView(info.emitName);
    m_body += attributes;
    m_body += u'>';
    if (!(info.flags & Void)) {
        m_stack.push_back(OpenElement{tag, info.flags});
        ++m_open[index(tag)];
    }
}

void Sanitizer::popTo(std::size_t size)
{
    while (m_stack.size() > size) {
        const Tag tag = m_stack.back().tag;
        m_stack.pop_back();
        --m_open[index(tag)];
        m_body += "</"_L1;
        m_body += QLatin1StringView(tagInfo(tag).emitName);
        m_body += u'>';
    }
    while (!m_tables.empty() && std::size_t(m_tables.back().table) >= size)
        m_tables.pop_back();
    if (!m_tables.empty()) {
        TableState &table = m_tables.back();
        const int top = int(size);
        if (table.cell >= top)
            table.cell = -1;
        if (table.row >= top)
            table.row = -1;
        if (table.section >= top)
            table.section = -1;
    }
}

void Sanitizer::closeImplied(Tag tag)
{
    const quint16 flags = tagInfo(tag).flags;
    const auto closeAt = [this](int at) {
        if (at >= 0)
            popTo(std::size_t(at));
    };
    if ((flags & Block) && m_open[index(Tag::P)] > 0)
        closeAt(findOpen(m_stack, is(Tag::P), Scope));
    if ((flags & Heading) && !m_stack.empty() && (m_stack.back().flags & Heading))
        popTo(m_stack.size() - 1);
    if (tag == Tag::Li && m_open[index(Tag::Li)] > 0)
        closeAt(findListItem(m_stack, false));
    else if ((tag == Tag::Dd || tag == Tag::Dt) && m_open[index(Tag::Dd)] + m_open[index(Tag::Dt)] > 0)
        closeAt(findListItem(m_stack, true));
    else if (tag == Tag::A && m_open[index(Tag::A)] > 0) // links do not nest
        closeAt(findOpen(m_stack, is(Tag::A), Scope));
}

void Sanitizer::close(Tag tag)
{
    int at = -1;
    if (tagInfo(tag).flags & Heading) // any heading end tag closes the open heading (HTML5)
        at = findOpen(m_stack, [](const OpenElement &e) { return bool(e.flags & Heading); }, Scope);
    else if (m_open[index(tag)] == 0)
        return;
    else if (tag == Tag::Li || tag == Tag::Dd || tag == Tag::Dt)
        at = findOpen(m_stack, is(tag), Scope | ListBox);
    else
        at = findOpen(m_stack, is(tag), Scope);
    if (at >= 0)
        popTo(std::size_t(at));
}

void Sanitizer::lineBreak()
{
    ensureFlow();
    m_contentStarted = true;
    m_body += "<br>"_L1;
}

bool Sanitizer::atTableLevel() const
{
    return !m_tables.empty() && !m_stack.empty() && isTableStructure(m_stack.back().tag);
}

// Content is about to be written. Inside a table but outside any cell, Qt
// drops it (measured), so it gets a cell; HTML5 would move it before the
// table, keeping it in place is the simpler visible equivalent.
void Sanitizer::ensureFlow()
{
    if (atTableLevel())
        openCell(Tag::Td, nullptr, true); // opens the row it needs, budget or not
}

void Sanitizer::openTable(const HtmlToken &token)
{
    closeImplied(Tag::Table);
    ensureFlow();
    if (int(m_tables.size()) >= m_options.maxTableDepth || m_usedCells >= kMaxTableCells
        || m_stack.size() + 4 > std::size_t(kMaxOpenElements)) {
        ++m_suppressedTables; // flattened: see suppressedTableTag()
        return;
    }
    openWith(Tag::Table, attributes(Tag::Table, token));
    TableState table;
    table.table = int(m_stack.size()) - 1;
    m_tables.push_back(table);
}

void Sanitizer::tableStructure(Tag tag, const HtmlToken &token)
{
    if (m_tables.empty())
        return; // outside a table HTML5 ignores these tags; the content flows on
    if (tag == Tag::Tr)
        openRow(&token, false);
    else if (tag == Tag::Td || tag == Tag::Th)
        openCell(tag, &token, false);
    else
        openSection(tag, token);
}

// `force`: the row must exist (content needs a cell), budget or not — the
// overshoot is one row of an already open table.
void Sanitizer::openRow(const HtmlToken *token, bool force)
{
    TableState &table = m_tables.back();
    if (!force && (table.frozen || m_usedCells + table.cols > kMaxTableCells)) {
        table.frozen = true; // the rest of this table flows into its current cell
        if (table.cell >= 0) {
            popTo(std::size_t(table.cell) + 1);
            m_body += "<br>"_L1;
        }
        table.rowClosed = false;
        return;
    }
    popTo(std::size_t(table.section >= 0 ? table.section : table.table) + 1);
    openWith(Tag::Tr, token ? attributes(Tag::Tr, *token) : QString());
    TableState &current = m_tables.back();
    current.row = int(m_stack.size()) - 1;
    current.cell = -1;
    current.rowCols = 0;
    current.rowClosed = false;
    ++current.rows;
    m_usedCells += current.cols;
}

void Sanitizer::openCell(Tag tag, const HtmlToken *token, bool force)
{
    if (m_tables.back().row < 0 || m_tables.back().rowClosed)
        openRow(nullptr, force);
    TableState &table = m_tables.back();
    if (table.frozen && table.cell >= 0 && !force) {
        separateCell(table);
        return;
    }
    int span = token ? colspanOf(*token) : 1;
    const auto widening = [&table](int cells) {
        const int columns = table.rowCols + cells;
        return columns > table.cols ? qint64(columns - table.cols) * table.rows : qint64(0);
    };
    if (table.rowCols + span > kMaxTableColumns || m_usedCells + widening(span) > kMaxTableCells) {
        if (table.cell >= 0 && !force) {
            separateCell(table);
            return;
        }
        // The row's first cell must exist: take no new column when possible.
        span = qMax(1, qMin(span, table.cols - table.rowCols));
    }
    const qint64 cost = widening(span);
    popTo(std::size_t(table.row) + 1);
    openWith(tag, token ? attributes(tag, *token, span) : QString());
    TableState &current = m_tables.back();
    current.cell = int(m_stack.size()) - 1;
    current.rowCols += span;
    if (current.rowCols > current.cols) {
        m_usedCells += cost;
        current.cols = current.rowCols;
    }
}

void Sanitizer::separateCell(TableState &table)
{
    popTo(std::size_t(table.cell) + 1);
    m_body += u' ';
}

void Sanitizer::openSection(Tag tag, const HtmlToken &token)
{
    if (m_tables.back().frozen)
        return;
    popTo(std::size_t(m_tables.back().table) + 1);
    openWith(tag, attributes(tag, token));
    m_tables.back().section = int(m_stack.size()) - 1;
}

void Sanitizer::tableEnd(Tag tag)
{
    if (m_tables.empty())
        return;
    TableState &table = m_tables.back();
    if (tag == Tag::Table) {
        popTo(std::size_t(table.table));
    } else if ((tag == Tag::Td || tag == Tag::Th) && table.cell >= 0) {
        popTo(std::size_t(table.cell) + 1); // the cell itself stays open, see above
    } else if (tag == Tag::Tr && table.row >= 0) {
        popTo(std::size_t(table.cell >= 0 ? table.cell : table.row) + 1);
        table.rowClosed = true;
    }
    // </thead> </tbody> </tfoot>: a section closes when the next one opens.
}

// Tags of a table that is being flattened (over maxTableDepth or the cell
// budget): rows become line breaks, cells spaces, the rest disappears.
void Sanitizer::suppressedTableTag(Tag tag, bool start)
{
    if (tag == Tag::Table) {
        m_suppressedTables += start ? 1 : -1;
    } else if (start && tag == Tag::Tr) {
        lineBreak();
    } else if (start && (tag == Tag::Td || tag == Tag::Th)) {
        ensureFlow();
        m_body += u' ';
    }
}

} // namespace QindaOffice::Mail::Detail
