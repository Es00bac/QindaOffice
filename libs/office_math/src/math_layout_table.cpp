// SPDX-License-Identifier: GPL-3.0-or-later
// Delimiters, equation arrays and matrices.
#include "math_layout_p.h"

namespace QindaOffice::Math {

namespace {

char32_t firstCodePoint(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.isEmpty() ? 0 : char32_t(chars.first());
}

// Where a block of rows sits: its middle on the math axis (m:baseJc
// center, the default), or its first or last row on the baseline.
qreal blockShift(VerticalJustification jc, qreal top, qreal bottom, qreal lastBaseline, qreal axis)
{
    switch (jc) {
    case VerticalJustification::Top:
        return 0;
    case VerticalJustification::Bottom:
        return -lastBaseline;
    case VerticalJustification::Center:
        break;
    }
    return -axis - (top + bottom) / 2;
}

} // namespace

qreal Layouter::rowSkip(const Node &n, Style st) const
{
    const qreal single = 1.3 * em(st);
    switch (rowSpacingRule(n)) {
    case 1:
        return single * 1.5;
    case 2:
        return single * 2;
    case 3: // exactly m:rSp twips
        return rowSpacing(n) / 20.0 * m_options.dpi / 72.0;
    case 4: // m:rSp half-lines
        return single * qMax(1, rowSpacing(n)) / 2.0;
    default:
        return single;
    }
}

// m:d: content, separators and fences; growing fences cover the content
// around the math axis (TeX rule 19 with a smaller shortfall than TeX's
// 5pt, so brackets clear matrices as Word's do), or match its height and
// depth with m:shp="match".
Box Layouter::delimiter(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    std::vector<Box> parts;
    qreal ascent = 0;
    qreal descent = 0;
    for (int i = 0; i < n.args.size(); ++i) {
        Box e = argBox(n, i, st, path, item);
        e.absorbItalic();
        ascent = qMax(ascent, e.ascent);
        descent = qMax(descent, e.descent);
        parts.push_back(std::move(e));
    }
    const bool grow = delimiterGrows(n);
    const bool match = delimiterShape(n) == DelimiterShape::Match;
    const qreal ax = axis(st);
    const qreal span = match ? ascent + descent : 2 * qMax(ascent - ax, descent + ax);
    const qreal target = span - qMin(0.1 * span, 0.15 * em(st));
    const qreal centre = match ? (ascent - descent) / 2 : ax;
    const qreal px = pixelSize(st);
    const auto fence = [&](const QString &chr) {
        const char32_t c = firstCodePoint(chr);
        if (c == 0) {
            return Box::group();
        }
        const GlyphInfo glyph = m_font.glyph(c, px);
        if (!grow) {
            return glyphBox(glyph, color);
        }
        const StretchedGlyph s = m_font.stretch(glyph, Qt::Vertical, target, px);
        Box b = Box::group();
        b.add(stretchedBox(s, color), QPointF(0, -(centre - (s.ascent - s.descent) / 2)));
        b.fitChildren();
        b.width = s.width;
        return b;
    };
    Box out = Box::group();
    qreal x = 0;
    const auto place = [&out, &x](Box b) {
        const qreal w = b.width;
        out.add(std::move(b), QPointF(x, 0));
        x += w;
    };
    place(fence(delimiterBegin(n)));
    const QString separator = delimiterSeparator(n);
    for (size_t i = 0; i < parts.size(); ++i) {
        place(std::move(parts.at(i)));
        if (i + 1 < parts.size()) {
            place(fence(separator));
        }
    }
    place(fence(delimiterEnd(n)));
    out.fitChildren();
    out.width = x;
    return out;
}

// m:eqArr: one row per m:e; '&' in a row marks alignment points — the
// columns between them alternate right- and left-aligned across the rows
// (as amsmath's align), with an em between column pairs; rows without any
// are centred. The block is centred on the axis unless m:baseJc says.
Box Layouter::eqArray(const Node &n, Style st, const SlotPath *path, int item)
{
    struct Row {
        QList<Piece> pieces;
        QList<qreal> xs;
        qreal width = 0;
        QList<int> marks;
        Style style;
        SlotPath path;
    };
    QList<Row> rows;
    int columns = 1;
    for (int r = 0; r < n.args.size(); ++r) {
        const Argument &arg = n.args.at(r);
        Row row;
        row.style = st;
        if (const int size = argumentSize(arg); size != 0) {
            row.style.level = qBound(-2, st.level - size, 2);
        }
        if (path != nullptr) {
            row.path = path->child(item, r);
        }
        row.pieces = pieces(arg, row.style, path != nullptr ? &row.path : nullptr);
        row.width = positions(row.pieces, row.style, &row.xs);
        for (int i = 0; i < row.pieces.size(); ++i) {
            if (row.pieces.at(i).alignMark) {
                row.marks.append(i);
            }
        }
        columns = qMax(columns, int(row.marks.size()) + 1);
        rows.append(std::move(row));
    }
    const auto columnStart = [](const Row &row, int k) { return k == 0 ? 0.0 : row.xs.at(row.marks.at(k - 1)); };
    const auto columnEnd = [](const Row &row, int k) {
        return k < row.marks.size() ? row.xs.at(row.marks.at(k)) : row.width;
    };
    QList<qreal> widths(columns, 0);
    for (const Row &row : std::as_const(rows)) {
        for (int k = 0; k <= row.marks.size(); ++k) {
            widths[k] = qMax(widths.at(k), columnEnd(row, k) - columnStart(row, k));
        }
    }
    const bool aligned = columns > 1;
    QList<qreal> starts(columns, 0);
    qreal total = 0;
    for (int k = 0; k < columns; ++k) {
        if (k > 0 && k % 2 == 0) {
            total += em(st);
        }
        starts[k] = total;
        total += widths.at(k);
    }
    std::vector<Box> boxes;
    for (Row &row : rows) {
        QList<qreal> xs = row.xs;
        qreal end = row.width;
        for (int k = 0; k <= row.marks.size(); ++k) {
            const qreal natural = columnEnd(row, k) - columnStart(row, k);
            qreal target = starts.at(k) + (k % 2 == 0 ? widths.at(k) - natural : 0);
            if (!aligned) {
                target = (total - natural) / 2;
            }
            const qreal delta = target - columnStart(row, k);
            const int first = k == 0 ? 0 : row.marks.at(k - 1);
            const int last = k < row.marks.size() ? row.marks.at(k) : int(row.pieces.size());
            for (int i = first; i < last; ++i) {
                xs[i] += delta;
            }
            if (k == row.marks.size()) {
                end = row.width + delta;
            }
        }
        boxes.push_back(assemble(row.pieces, xs, end, row.style, path != nullptr ? &row.path : nullptr, true));
    }
    const qreal skip = rowSkip(n, st);
    const qreal gap = 0.25 * em(st);
    QList<qreal> ys;
    for (size_t r = 0; r < boxes.size(); ++r) {
        ys.append(r == 0 ? 0 : ys.last() + qMax(skip, boxes.at(r - 1).descent + boxes.at(r).ascent + gap));
    }
    Box out = Box::group();
    if (boxes.empty()) {
        return out;
    }
    const qreal shift = blockShift(baseJustification(n), -boxes.front().ascent, ys.last() + boxes.back().descent,
                                   ys.last(), axis(st));
    for (size_t r = 0; r < boxes.size(); ++r) {
        out.add(std::move(boxes.at(r)), QPointF(0, ys.at(qsizetype(r)) + shift));
    }
    out.fitChildren();
    out.width = qMax(out.width, total);
    return out;
}

// m:m: cells in a grid — columns as wide as their widest cell and aligned
// as m:mcs says (centred by default), rows at least a strut tall and a
// row skip apart, the column gap from m:cGpRule; centred on the axis.
Box Layouter::matrix(const Node &n, Style st, const SlotPath *path, int item)
{
    const int columns = columnCount(n);
    const int rowCount = int(n.rowSizes.size());
    const bool showPlaceholders = !placeholdersHidden(n);
    const qreal strutAscent = 0.7 * em(st);
    const qreal strutDescent = 0.25 * em(st);
    std::vector<std::vector<Box>> cells(static_cast<size_t>(rowCount));
    QList<qreal> widths(columns, 0);
    QList<qreal> ascents(rowCount, strutAscent);
    QList<qreal> descents(rowCount, strutDescent);
    int index = 0;
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < n.rowSizes.at(r); ++c) {
            Box cell = argBox(n, index++, st, path, item, showPlaceholders);
            cell.absorbItalic();
            widths[c] = qMax(widths.at(c), cell.width);
            ascents[r] = qMax(ascents.at(r), cell.ascent);
            descents[r] = qMax(descents.at(r), cell.descent);
            cells[size_t(r)].push_back(std::move(cell));
        }
    }
    const qreal minWidth = columnSpacing(n) / 20.0 * m_options.dpi / 72.0;
    qreal gap = em(st);
    switch (columnGapRule(n)) {
    case 1:
        gap = 1.5 * em(st);
        break;
    case 2:
        gap = 2 * em(st);
        break;
    case 3:
        gap = columnGap(n) / 20.0 * m_options.dpi / 72.0;
        break;
    case 4:
        gap = columnGap(n) * em(st) / 2.0;
        break;
    default:
        break;
    }
    QList<qreal> xs(columns, 0);
    qreal x = 0;
    for (int c = 0; c < columns; ++c) {
        widths[c] = qMax(widths.at(c), minWidth);
        xs[c] = x;
        x += widths.at(c) + (c + 1 < columns ? gap : 0);
    }
    const qreal total = x;
    const qreal skip = rowSkip(n, st);
    const qreal rowGap = 0.2 * em(st);
    QList<qreal> ys;
    for (int r = 0; r < rowCount; ++r) {
        ys.append(r == 0 ? 0 : ys.last() + qMax(skip, descents.at(r - 1) + ascents.at(r) + rowGap));
    }
    Box out = Box::group();
    if (rowCount == 0) {
        return out;
    }
    const qreal shift =
        blockShift(baseJustification(n), -ascents.first(), ys.last() + descents.last(), ys.last(), axis(st));
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < int(cells[size_t(r)].size()); ++c) {
            Box &cell = cells[size_t(r)][size_t(c)];
            qreal cx = xs.at(c) + (widths.at(c) - cell.width) / 2;
            switch (columnAlignment(n, c)) {
            case ColumnAlignment::Left:
                cx = xs.at(c);
                break;
            case ColumnAlignment::Right:
                cx = xs.at(c) + widths.at(c) - cell.width;
                break;
            case ColumnAlignment::Center:
                break;
            }
            out.add(std::move(cell), QPointF(cx, ys.at(r) + shift));
        }
    }
    out.fitChildren();
    out.ascent = qMax(out.ascent, ascents.first() - (ys.first() + shift));
    out.descent = qMax(out.descent, ys.last() + shift + descents.last());
    out.width = total;
    return out;
}

} // namespace QindaOffice::Math
