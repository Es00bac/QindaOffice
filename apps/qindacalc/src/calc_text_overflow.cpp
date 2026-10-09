// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's text overflow and number fitting (calc_text_overflow.h, M82,
// D-091).

#include "calc_text_overflow.h"

#include "value.h"

#include <QList>
#include <QPair>

#include <cmath>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// AGENT-GUARD: a spill walks at most this many columns. Hidden columns
// take no width, so without a bound a run of them (a collapsed outline of
// thousands of columns) would be walked cell by cell on every paint.
constexpr int kMaxSpillColumns = 2048;

// What stops a spill in one row: cells with content, and the merged ranges
// crossing the row — gathered once, not looked up per column (a sheet's
// merge list is searched linearly).
class RowStops {
public:
    RowStops(const Sheet &sheet, qint32 row)
        : m_sheet(sheet)
        , m_row(row)
    {
        for (const CellRange &merge : sheet.merges()) {
            if (merge.topLeft.row <= row && merge.bottomRight.row >= row) {
                m_merged.append({merge.topLeft.col, merge.bottomRight.col});
            }
        }
    }

    [[nodiscard]] bool operator()(qint32 col) const
    {
        for (const auto &[first, last] : m_merged) {
            if (col >= first && col <= last) {
                return true;
            }
        }
        const Cell *cell = m_sheet.cellAt({m_row, col});
        return cell != nullptr && (cell->hasFormula() || !isBlank(cell->value));
    }

private:
    const Sheet &m_sheet;
    qint32 m_row;
    QList<QPair<qint32, qint32>> m_merged;
};

// Walks from `col` by `step` over empty cells until `wanted` is covered;
// the last column reached (`col` itself when the first neighbour stops it).
qint32 extend(const RowStops &stops, qint32 col, int step, qreal wanted, const ColumnWidth &width, qint32 minCol,
              qint32 maxCol)
{
    qint32 last = col;
    qreal covered = 0;
    int walked = 0;
    for (qint32 c = col + step; covered < wanted && c >= minCol && c <= maxCol && walked < kMaxSpillColumns;
         c += step, ++walked) {
        if (stops(c)) {
            break;
        }
        covered += width(c);
        last = c;
    }
    return last;
}

// "1.20E+08" → "1.2E+08", "1.0E+08" → "1E+08": General never pads.
QString trimmedScientific(double value, int digits)
{
    QString text = QString::number(value, 'E', digits);
    const qsizetype e = text.indexOf(QLatin1Char('E'));
    QString mantissa = text.left(e);
    if (mantissa.contains(QLatin1Char('.'))) {
        while (mantissa.endsWith(QLatin1Char('0'))) {
            mantissa.chop(1);
        }
        if (mantissa.endsWith(QLatin1Char('.'))) {
            mantissa.chop(1);
        }
    }
    return mantissa + text.mid(e);
}

QString trimmedFixed(double value, int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

bool hasSignificantDigit(const QString &text)
{
    for (const QChar c : text) {
        if (c.isDigit() && c != QLatin1Char('0')) {
            return true;
        }
    }
    return false;
}

} // namespace

CellHorizontal horizontalLayout(const Value &value, const CellStyle *style, bool merged)
{
    CellHorizontal out;
    out.numeric = isNumber(value);
    const int alignment = style != nullptr ? style->alignment : 0;
    if (alignment == kAlignFill) {
        out.fill = true;
        return out; // repeated from the left edge; never spills
    }
    if (alignment == 1) {
        out.align = Qt::AlignHCenter;
    } else if (alignment == 2) {
        out.align = Qt::AlignRight;
    } else if (out.numeric) {
        out.align = Qt::AlignRight; // General: numbers right,
    } else if (isBool(value) || isErr(value)) {
        out.align = Qt::AlignHCenter; // booleans and errors in the middle,
    } else {
        out.align = Qt::AlignLeft; // text left
    }
    // Only text spills: never wrapped, merged or filled text.
    if (!isText(value) || merged || (style != nullptr && style->wrap)) {
        return out;
    }
    out.spill = out.align == Qt::AlignHCenter ? SpillDirection::Both
        : out.align == Qt::AlignRight         ? SpillDirection::Left
                                              : SpillDirection::Right;
    return out;
}

bool stopsSpill(const Sheet &sheet, const CellAddr &addr)
{
    return RowStops(sheet, addr.row)(addr.col);
}

SpillSpan spillSpan(const Sheet &sheet, const CellAddr &addr, SpillDirection direction, qreal need,
                    const ColumnWidth &width, qint32 minCol, qint32 maxCol)
{
    SpillSpan span{addr.col, addr.col};
    const qreal own = width(addr.col);
    if (direction == SpillDirection::None || need <= own) {
        return span;
    }
    const RowStops stops(sheet, addr.row);
    const qreal overflow = need - own;
    switch (direction) {
    case SpillDirection::Right:
        span.last = extend(stops, addr.col, 1, overflow, width, minCol, maxCol);
        break;
    case SpillDirection::Left:
        span.first = extend(stops, addr.col, -1, overflow, width, minCol, maxCol);
        break;
    case SpillDirection::Both:
        span.first = extend(stops, addr.col, -1, overflow / 2, width, minCol, maxCol);
        span.last = extend(stops, addr.col, 1, overflow / 2, width, minCol, maxCol);
        break;
    case SpillDirection::None:
        break;
    }
    return span;
}

qint32 nearestStop(const Sheet &sheet, qint32 row, qint32 from, int step, qint32 bound, const ColumnWidth &width,
                   qreal reach)
{
    const RowStops stops(sheet, row);
    qreal walked = 0;
    int columns = 0;
    for (qint32 c = from; (step < 0 ? c >= bound : c <= bound) && columns < kMaxSpillColumns; c += step, ++columns) {
        if (stops(c)) {
            return c;
        }
        walked += width(c);
        if (walked > reach) {
            return -1;
        }
    }
    return -1;
}

QString fittedNumber(double value, const QString &formatted, bool general, qreal room,
                     const std::function<qreal(const QString &)> &measure)
{
    if (measure(formatted) <= room) {
        return formatted;
    }
    if (general && std::isfinite(value)) {
        // Fewer decimal places while a significant digit is left (Excel
        // shows 3.14159 as 3.1416, 3.14, 3 as the column narrows) ...
        if (value == 0 || std::abs(value) >= 1e-4) {
            for (int decimals = 10; decimals >= 0; --decimals) {
                const QString text = trimmedFixed(value, decimals);
                if ((value == 0 || hasSignificantDigit(text)) && measure(text) <= room) {
                    return text;
                }
            }
        }
        // ... then scientific notation with fewer and fewer digits.
        for (int digits = 5; digits >= 0; --digits) {
            const QString text = trimmedScientific(value, digits);
            if (measure(text) <= room) {
                return text;
            }
        }
    }
    const qreal hash = measure(QStringLiteral("#"));
    const int count = hash > 0 ? qMax(1, int(std::floor(room / hash))) : 1;
    return QString(count, QLatin1Char('#'));
}

QString filledText(const QString &text, qreal room, const std::function<qreal(const QString &)> &measure)
{
    const qreal once = measure(text);
    if (text.isEmpty() || once <= 0) {
        return text;
    }
    return text.repeated(qMax(1, int(std::floor(room / once))));
}

} // namespace QindaOffice::Apps::QindaCalc
