// SPDX-License-Identifier: GPL-3.0-or-later
// Goal Seek (goal_seek.h, M78, D-087). Each value tried is one iteration:
// it goes into the changing cell (setCellValue, so its style stays and the
// workbook recalculates), and the set cell's distance from the target is
// read back. Unbracketed, the search takes secant steps from the cell's
// value (the first a small step); once two values give results on both
// sides of the target it keeps that bracket and closes it with the
// Illinois variant of regula falsi, bisecting when that stalls. The value
// that came closest is the one left in the cell.

#include "goal_seek.h"

#include "sheet.h"
#include "what_if_internal.h"
#include "workbook.h"

#include <cmath>
#include <optional>

namespace QindaOffice::Calc {

namespace {

struct Point {
    double x = 0;
    double f = 0; // the set cell's value minus the target
};

// A result Goal Seek can work with: a number (a blank is 0, as in Excel).
std::optional<double> numberOf(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return std::isfinite(*number) ? std::optional<double>(*number) : std::nullopt;
    }
    if (isBlank(value)) {
        return 0.0;
    }
    return std::nullopt;
}

class Search {
public:
    Search(Workbook &book, qint32 sheet, const CellAddr &setCell, double target, const CellAddr &changing,
           const GoalSeekOptions &options)
        : m_book(book)
        , m_sheet(sheet)
        , m_set(setCell)
        , m_target(target)
        , m_changing(changing)
        , m_options(options)
    {
    }

    // f(x), or nullopt when the set cell does not give a number there.
    std::optional<double> at(double x)
    {
        ++m_iterations;
        m_book.setCellValue(m_sheet, m_changing, Value(x));
        m_last = x;
        const std::optional<double> value = numberOf(m_book.cellValue(m_sheet, m_set.row, m_set.col));
        if (!value.has_value()) {
            return std::nullopt;
        }
        const double f = *value - m_target;
        if (!m_best.has_value() || std::abs(f) < std::abs(m_best->f)) {
            m_best = Point{x, f};
        }
        return f;
    }

    [[nodiscard]] bool done() const
    {
        return (m_best.has_value() && std::abs(m_best->f) <= m_options.maxChange)
            || m_iterations >= m_options.maxIterations;
    }

    void run(double x0, std::optional<double> f0)
    {
        if (f0.has_value()) {
            m_best = Point{x0, *f0};
        }
        std::optional<Point> previous = f0.has_value() ? std::optional(Point{x0, *f0}) : std::nullopt;
        double xPrev = x0;
        double x = x0 + (x0 != 0.0 ? std::abs(x0) * 1e-3 : 1e-3);
        while (!done()) {
            const std::optional<double> f = at(x);
            if (done()) {
                return;
            }
            if (!f.has_value()) {
                // No number here: back off halfway towards the last value
                // that gave one.
                const double back = previous.has_value() ? previous->x : xPrev;
                xPrev = x;
                x = (x + back) / 2.0;
                continue;
            }
            const Point current{x, *f};
            if (previous.has_value() && std::signbit(previous->f) != std::signbit(current.f)) {
                bracket(*previous, current);
                return;
            }
            double next = 0;
            if (previous.has_value() && current.f != previous->f) {
                next = current.x - current.f * (current.x - previous->x) / (current.f - previous->f); // secant
            } else {
                next = current.x + 2.0 * (current.x - xPrev); // flat: step further out
            }
            if (!std::isfinite(next) || next == current.x) {
                next = current.x + (current.x != 0.0 ? std::abs(current.x) : 1.0);
            }
            xPrev = current.x;
            previous = current;
            x = next;
        }
    }

    [[nodiscard]] int iterations() const { return m_iterations; }
    [[nodiscard]] const std::optional<Point> &best() const { return m_best; }
    [[nodiscard]] double last() const { return m_last; }

private:
    // Illinois regula falsi over a sign change, bisection when it stalls.
    void bracket(Point a, Point b)
    {
        int retained = 0; // which end kept its place last step: -1 a, +1 b
        double width = std::abs(b.x - a.x);
        while (!done()) {
            double x = (a.x * b.f - b.x * a.f) / (b.f - a.f);
            const double lo = std::min(a.x, b.x);
            const double hi = std::max(a.x, b.x);
            if (!std::isfinite(x) || x <= lo || x >= hi) {
                x = (a.x + b.x) / 2.0;
            }
            const std::optional<double> f = at(x);
            if (done()) {
                return;
            }
            if (!f.has_value()) {
                // Nothing to read: keep the end with the smaller distance,
                // moving the other one in (its sign is assumed to hold).
                (std::abs(a.f) <= std::abs(b.f) ? b.x : a.x) = x;
                continue;
            }
            if (std::signbit(*f) == std::signbit(a.f)) {
                a = Point{x, *f};
                if (retained == 1) {
                    b.f /= 2.0; // b kept its place twice: weigh it down
                }
                retained = 1;
            } else {
                b = Point{x, *f};
                if (retained == -1) {
                    a.f /= 2.0;
                }
                retained = -1;
            }
            if (std::abs(b.x - a.x) > width / 2.0) {
                // Not halving: bisect once more next round.
                const double mid = (a.x + b.x) / 2.0;
                const std::optional<double> fm = at(mid);
                if (done() || !fm.has_value()) {
                    continue;
                }
                if (std::signbit(*fm) == std::signbit(a.f)) {
                    a = Point{mid, *fm};
                } else {
                    b = Point{mid, *fm};
                }
                retained = 0;
            }
            width = std::abs(b.x - a.x);
        }
    }

    Workbook &m_book;
    qint32 m_sheet;
    CellAddr m_set;
    double m_target;
    CellAddr m_changing;
    GoalSeekOptions m_options;
    int m_iterations = 0;
    double m_last = 0;
    std::optional<Point> m_best;
};

} // namespace

GoalSeekResult goalSeek(Workbook &book, qint32 sheet, const CellAddr &setCell, double target,
                        const CellAddr &changingCell, const GoalSeekOptions &options)
{
    GoalSeekResult result;
    if (sheet < 0 || sheet >= book.sheetCount() || !setCell.isValid() || !changingCell.isValid()) {
        result.error = QStringLiteral("Reference is not valid.");
        return result;
    }
    const Sheet &where = book.sheetAt(sheet);
    const Cell *set = where.cellAt(setCell);
    if (set == nullptr || !set->hasFormula()) {
        result.error = QStringLiteral("Cell must contain a formula.");
        return result;
    }
    const Cell *changing = where.cellAt(changingCell);
    if (changing != nullptr && (changing->hasFormula() || changing->isSpilled()
                                || !numberOf(changing->value).has_value())) {
        // A formula, a spilled result, text, a boolean or an error: Excel
        // changes only a cell that holds a number (or nothing).
        result.error = QStringLiteral("Cell must contain a value.");
        return result;
    }
    const Value now = book.cellValue(sheet, setCell.row, setCell.col);
    if (!isErr(now) && !numberOf(now).has_value()) {
        // Text or a boolean: there is no number to bring to the target.
        result.error = QStringLiteral("Cell must contain a formula that gives a number.");
        return result;
    }
    const WhatIf::SavedCell original = WhatIf::saveCell(book, sheet, changingCell);
    // AGENT-GUARD: `set` and `changing` point into the sheet's cell hash;
    // the first value tried may rehash it. Nothing below reads them.
    const double x0 = changing != nullptr ? numberOf(changing->value).value_or(0.0) : 0.0;
    const std::optional<double> current = numberOf(now);
    const std::optional<double> f0 = current.has_value() ? std::optional(*current - target) : std::nullopt;
    result.started = true;
    GoalSeekOptions bounded = options;
    bounded.maxIterations = qMax(1, options.maxIterations);
    bounded.maxChange = std::abs(options.maxChange);
    if (f0.has_value() && std::abs(*f0) <= bounded.maxChange) {
        // Already there: nothing changes (a blank changing cell stays blank).
        result.found = true;
        result.changingValue = x0;
        result.setValue = *current;
        return result;
    }
    Search search(book, sheet, setCell, target, changingCell, bounded);
    search.run(x0, f0);
    result.iterations = search.iterations();
    if (!search.best().has_value()) {
        // No value tried gave a number: the cell gets its own content back.
        book.recalc({WhatIf::restoreCell(book, sheet, original)});
        result.changingValue = x0;
        return result;
    }
    const Point best = *search.best();
    if (f0.has_value() && best.x == x0 && best.f == *f0) {
        // Nothing came closer than where it started: the cell gets its own
        // content back (a blank stays blank).
        book.recalc({WhatIf::restoreCell(book, sheet, original)});
    } else if (search.last() != best.x) {
        book.setCellValue(sheet, changingCell, Value(best.x));
    }
    result.changingValue = best.x;
    result.setValue = numberOf(book.cellValue(sheet, setCell.row, setCell.col)).value_or(best.f + target);
    result.found = std::abs(result.setValue - target) <= bounded.maxChange;
    return result;
}

} // namespace QindaOffice::Calc
