// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet.h"

#include "cond_format_internal.h"

namespace QindaOffice::Calc {

namespace {
constexpr qreal kDefaultColWidth = 96.0;  // 64 pt char column ~= 96 px at 96 dpi (plan: width units)
constexpr qreal kDefaultRowHeight = 20.0; // 15 pt ~= 20 px
} // namespace

Sheet::Sheet(QString name)
    : m_name(std::move(name))
{
}

Cell *Sheet::cellAt(const CellAddr &addr)
{
    return const_cast<Cell *>(std::as_const(*this).cellAt(addr));
}

const Cell *Sheet::cellAt(const CellAddr &addr) const
{
    const auto it = m_cells.constFind(addr.key());
    return it == m_cells.constEnd() ? nullptr : &it.value();
}

Cell &Sheet::ensureCell(const CellAddr &addr)
{
    return m_cells[addr.key()];
}

bool Sheet::clearCell(const CellAddr &addr)
{
    m_styleIds.remove(addr.key());
    return m_cells.remove(addr.key()) > 0;
}

qreal Sheet::columnWidth(qint32 col) const
{
    return m_colWidths.value(col, kDefaultColWidth);
}

qreal Sheet::rowHeight(qint32 row) const
{
    return m_rowHeights.value(row, kDefaultRowHeight);
}

void Sheet::setColumnWidth(qint32 col, qreal px)
{
    m_colWidths.insert(col, px);
}

void Sheet::setRowHeight(qint32 row, qreal px)
{
    m_rowHeights.insert(row, px);
}

qint32 Sheet::styleIdAt(const CellAddr &addr) const
{
    return m_styleIds.value(addr.key(), 0);
}

void Sheet::setStyleIdAt(const CellAddr &addr, qint32 id)
{
    m_styleIds.insert(addr.key(), id);
}

bool Sheet::removeMergeAt(const CellAddr &anchor)
{
    for (int i = 0; i < m_merges.size(); ++i) {
        if (m_merges.at(i).topLeft == anchor) {
            m_merges.removeAt(i);
            return true;
        }
    }
    return false;
}

const CellRange *Sheet::mergeCovering(const CellAddr &addr) const
{
    for (const CellRange &range : m_merges) {
        if (range.contains(addr)) {
            return &range;
        }
    }
    return nullptr;
}

void Sheet::replaceContents(QHash<quint64, Cell> cells, QHash<quint64, qint32> styleIds,
                            QHash<qint32, qreal> colWidths, QHash<qint32, qreal> rowHeights,
                            QList<CellRange> merges)
{
    m_cells = std::move(cells);
    m_styleIds = std::move(styleIds);
    m_colWidths = std::move(colWidths);
    m_rowHeights = std::move(rowHeights);
    m_merges = std::move(merges);
}

CondFormat Sheet::condFormatFor(const CellAddr &addr, const Value &value) const
{
    // The classic-ops path (tests and callers without a workbook); the app
    // evaluates through Workbook::condFormatFor, which also resolves the
    // kinds that need the workbook — formulas, statistics, scales, bars and
    // icons (condRuleNeedsHost). The merge is first-wins-per-field with
    // stopIfTrue, Excel's model: the list is in precedence order, so a field
    // an earlier rule set is final.
    CondFormat merged;
    for (const CondFormatRule &rule : m_condFormats) {
        if (condRuleNeedsHost(rule) || !rule.covers(addr) || !condRuleMatches(rule, value)) {
            continue;
        }
        if (!merged.background.has_value() && rule.format.background.has_value()) {
            merged.background = rule.format.background;
        }
        if (!merged.foreground.has_value() && rule.format.foreground.has_value()) {
            merged.foreground = rule.format.foreground;
        }
        if (!merged.bold.has_value() && rule.format.bold.has_value()) {
            merged.bold = rule.format.bold;
        }
        if (!merged.italic.has_value() && rule.format.italic.has_value()) {
            merged.italic = rule.format.italic;
        }
        if (!merged.underline.has_value() && rule.format.underline.has_value()) {
            merged.underline = rule.format.underline;
        }
        if (!merged.strikeout.has_value() && rule.format.strikeout.has_value()) {
            merged.strikeout = rule.format.strikeout;
        }
        if (rule.stopIfTrue) {
            break;
        }
    }
    return merged;
}

const ValidationRule *Sheet::validationAt(const CellAddr &addr) const
{
    for (const ValidationRule &rule : m_validations) {
        if (rule.covers(addr)) {
            return &rule;
        }
    }
    return nullptr;
}

QSet<qint32> Sheet::filteredOutRows() const
{
    QSet<qint32> hidden;
    if (!m_filter.active || !m_filter.range.isValid() || m_filter.allowed.isEmpty()) {
        return hidden;
    }
    // The first row is the header and is never hidden — hiding it would
    // take the filter's own dropdowns off the screen.
    for (qint32 row = m_filter.range.topLeft.row + 1; row <= m_filter.range.bottomRight.row;
         ++row) {
        for (const auto &[column, allowed] : m_filter.allowed.asKeyValueRange()) {
            if (column < m_filter.range.topLeft.col || column > m_filter.range.bottomRight.col) {
                continue;
            }
            const Cell *cell = cellAt({row, column});
            const QString text = cell != nullptr ? toText(cell->value) : QString();
            if (!allowed.contains(text)) {
                hidden.insert(row);
                break;
            }
        }
    }
    return hidden;
}

QStringList Sheet::filterValuesFor(qint32 column,
                                   const std::function<QString(const CellAddr &)> &display) const
{
    QStringList values;
    if (!m_filter.range.isValid()) {
        return values;
    }
    for (qint32 row = m_filter.range.topLeft.row + 1; row <= m_filter.range.bottomRight.row;
         ++row) {
        const QString text = display ? display({row, column}) : QString();
        if (!values.contains(text)) {
            values.append(text);
        }
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

const CellComment *Sheet::commentAt(const CellAddr &addr) const
{
    const auto it = m_comments.constFind(addr.key());
    return it != m_comments.constEnd() ? &it.value() : nullptr;
}

const Hyperlink *Sheet::hyperlinkAt(const CellAddr &addr) const
{
    for (auto it = m_hyperlinks.crbegin(); it != m_hyperlinks.crend(); ++it) {
        if (it->range.contains(addr)) {
            return &*it;
        }
    }
    return nullptr;
}

const TableSpec *Sheet::tableCovering(const CellAddr &addr) const
{
    for (const TableSpec &table : m_tables) {
        if (table.range.contains(addr)) {
            return &table;
        }
    }
    return nullptr;
}

void Sheet::setCommentAt(const CellAddr &addr, const CellComment &comment)
{
    if (comment.text.isEmpty()) {
        m_comments.remove(addr.key());
        return;
    }
    m_comments.insert(addr.key(), comment);
}

bool Sheet::clearCommentAt(const CellAddr &addr)
{
    return m_comments.remove(addr.key()) > 0;
}

} // namespace QindaOffice::Calc
