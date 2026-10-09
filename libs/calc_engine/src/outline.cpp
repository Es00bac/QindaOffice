// SPDX-License-Identifier: GPL-3.0-or-later
// Row and column outlines (outline.h, M66, D-075).

#include "outline.h"

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

// The summary line of a group spanning [first, last].
qint32 summaryOf(const SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last)
{
    const bool after = axis == OutlineAxis::Rows ? outline.summaryBelow : outline.summaryRight;
    return after ? last + 1 : (first > 0 ? first - 1 : -1);
}

bool allHidden(const SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last)
{
    for (qint32 i = first; i <= last; ++i) {
        if (!outline.isHidden(axis, i)) {
            return false;
        }
    }
    return true;
}

} // namespace

void SheetOutline::setLine(OutlineAxis axis, qint32 index, const LineOutline &state)
{
    if (index < 0) {
        return;
    }
    if (state.isDefault()) {
        lines(axis).remove(index);
    } else {
        lines(axis).insert(index, state);
    }
}

quint8 SheetOutline::maxLevel(OutlineAxis axis) const
{
    quint8 deepest = 0;
    for (const LineOutline &line : lines(axis)) {
        deepest = std::max(deepest, line.level);
    }
    return deepest;
}

QList<OutlineGroup> outlineGroups(const SheetOutline &outline, OutlineAxis axis)
{
    QList<OutlineGroup> groups;
    const quint8 deepest = outline.maxLevel(axis);
    const QMap<qint32, LineOutline> &lines = outline.lines(axis);
    for (quint8 level = 1; level <= deepest; ++level) {
        qint32 first = -1;
        qint32 last = -2;
        const auto close = [&] {
            if (first >= 0) {
                groups.append({first, last, level, summaryOf(outline, axis, first, last),
                               allHidden(outline, axis, first, last)});
            }
        };
        // Only non-default lines are stored, so a run is consecutive keys
        // whose level reaches this one.
        for (auto it = lines.constBegin(); it != lines.constEnd(); ++it) {
            if (it.value().level < level) {
                continue;
            }
            if (it.key() == last + 1) {
                last = it.key();
                continue;
            }
            close();
            first = it.key();
            last = it.key();
        }
        close();
    }
    return groups;
}

std::optional<OutlineGroup> groupAt(const SheetOutline &outline, OutlineAxis axis, qint32 index)
{
    std::optional<OutlineGroup> inner;
    std::optional<OutlineGroup> bySummary;
    for (const OutlineGroup &group : outlineGroups(outline, axis)) {
        if (index >= group.first && index <= group.last) {
            if (!inner || group.level > inner->level) {
                inner = group;
            }
        } else if (group.summary == index) {
            // On a summary line: the outermost group it summarises, so Show
            // Detail opens one level at a time.
            if (!bySummary || group.level < bySummary->level) {
                bySummary = group;
            }
        }
    }
    return inner ? inner : bySummary;
}

bool groupLines(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last)
{
    bool changed = false;
    for (qint32 i = std::max<qint32>(0, first); i <= last; ++i) {
        LineOutline line = outline.line(axis, i);
        if (line.level < kMaxOutlineLevel) {
            ++line.level;
            outline.setLine(axis, i, line);
            changed = true;
        }
    }
    return changed;
}

bool ungroupLines(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last)
{
    bool changed = false;
    for (qint32 i = std::max<qint32>(0, first); i <= last; ++i) {
        LineOutline line = outline.line(axis, i);
        if (line.level > 0) {
            --line.level;
            outline.setLine(axis, i, line);
            changed = true;
        }
    }
    if (changed) {
        // A summary line whose group is gone is no longer collapsed.
        QList<qint32> summaries;
        for (const OutlineGroup &group : outlineGroups(outline, axis)) {
            summaries.append(group.summary);
        }
        for (const qint32 candidate : {first - 1, last + 1}) {
            LineOutline line = outline.line(axis, candidate);
            if (candidate >= 0 && line.collapsed && !summaries.contains(candidate)) {
                line.collapsed = false;
                outline.setLine(axis, candidate, line);
            }
        }
    }
    return changed;
}

void setGroupCollapsed(SheetOutline &outline, OutlineAxis axis, const OutlineGroup &group, bool collapse)
{
    // Inner groups the user collapsed on their own stay collapsed when the
    // outer one opens (their summary line keeps its mark).
    QList<OutlineGroup> keepHidden;
    if (!collapse) {
        for (const OutlineGroup &inner : outlineGroups(outline, axis)) {
            if (inner.level > group.level && inner.first >= group.first && inner.last <= group.last
                && inner.summary != group.summary && outline.line(axis, inner.summary).collapsed) {
                keepHidden.append(inner);
            }
        }
    }
    for (qint32 i = group.first; i <= group.last; ++i) {
        LineOutline line = outline.line(axis, i);
        line.hidden = collapse || std::any_of(keepHidden.cbegin(), keepHidden.cend(), [i](const OutlineGroup &inner) {
                          return i >= inner.first && i <= inner.last;
                      });
        outline.setLine(axis, i, line);
    }
    if (group.summary >= 0) {
        LineOutline summary = outline.line(axis, group.summary);
        summary.collapsed = collapse;
        outline.setLine(axis, group.summary, summary);
    }
}

void showOutlineLevel(SheetOutline &outline, OutlineAxis axis, int level)
{
    const QList<OutlineGroup> groups = outlineGroups(outline, axis);
    QMap<qint32, LineOutline> &lines = outline.lines(axis);
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (it.value().level > 0) {
            it.value().hidden = it.value().level >= level;
        }
    }
    for (const OutlineGroup &group : groups) {
        if (group.summary < 0) {
            continue;
        }
        LineOutline summary = outline.line(axis, group.summary);
        // A summary shared by nested groups is collapsed when any of them is.
        summary.collapsed = group.level >= level
            || std::any_of(groups.cbegin(), groups.cend(), [&](const OutlineGroup &other) {
                   return other.summary == group.summary && other.level >= level;
               });
        outline.setLine(axis, group.summary, summary);
    }
    // Entries that became default (unhidden, ungrouped) leave the map.
    for (auto it = lines.begin(); it != lines.end();) {
        it = it.value().isDefault() ? lines.erase(it) : std::next(it);
    }
}

void setLinesHidden(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last, bool hidden)
{
    for (qint32 i = std::max<qint32>(0, first); i <= last; ++i) {
        LineOutline line = outline.line(axis, i);
        line.hidden = hidden;
        outline.setLine(axis, i, line);
    }
}

} // namespace QindaOffice::Calc
