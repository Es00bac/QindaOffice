// SPDX-License-Identifier: GPL-3.0-or-later
// Source positions through the structural-edit journal (sheet_source.h).

#include "sheet_source.h"

namespace QindaOffice::Calc {

namespace {

// The same arithmetic structure.cpp applies to cells (movedIndex there).
std::optional<qint32> movedLine(const SourceStep &step, qint32 index)
{
    if (step.insert) {
        return index >= step.at ? index + step.count : index;
    }
    if (index >= step.at && index < step.at + step.count) {
        return std::nullopt;
    }
    return index >= step.at + step.count ? index - step.count : index;
}

// And the one it applies to references (movedSpan there).
bool movedSpan(const SourceStep &step, qint32 &lo, qint32 &hi)
{
    if (step.insert) {
        if (lo >= step.at) {
            lo += step.count;
        }
        if (hi >= step.at) {
            hi += step.count;
        }
        return true;
    }
    const qint32 first = step.at;
    const qint32 last = step.at + step.count - 1;
    if (lo >= first && hi <= last) {
        return false;
    }
    lo = lo > last ? lo - step.count : (lo >= first ? first : lo);
    hi = hi > last ? hi - step.count : (hi >= first ? first - 1 : hi);
    return lo <= hi;
}

} // namespace

std::optional<qint32> mapSourceLine(const QList<SourceStep> &steps, bool rows, qint32 index)
{
    std::optional<qint32> current = index;
    for (const SourceStep &step : steps) {
        if (step.rows == rows) {
            current = movedLine(step, *current);
            if (!current) {
                return std::nullopt;
            }
        }
    }
    return current;
}

std::optional<CellAddr> mapSourceCell(const QList<SourceStep> &steps, const CellAddr &addr)
{
    const auto row = mapSourceLine(steps, true, addr.row);
    const auto col = mapSourceLine(steps, false, addr.col);
    if (!row || !col) {
        return std::nullopt;
    }
    return CellAddr{*row, *col};
}

std::optional<CellRange> mapSourceRange(const QList<SourceStep> &steps, const CellRange &range)
{
    CellRange moved = range;
    for (const SourceStep &step : steps) {
        qint32 &lo = step.rows ? moved.topLeft.row : moved.topLeft.col;
        qint32 &hi = step.rows ? moved.bottomRight.row : moved.bottomRight.col;
        if (!movedSpan(step, lo, hi)) {
            return std::nullopt;
        }
    }
    return moved;
}

} // namespace QindaOffice::Calc
