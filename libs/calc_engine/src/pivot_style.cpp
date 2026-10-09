// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's built-in PivotTable styles (pivot_style.h; M74, D-083).
//
// AGENT-NOTE: the element list and its order are Excel's precedence for
// PivotTable styles (ECMA-376 tableStyleElement, lowest first): a later
// element's fill, font or rule wins over an earlier one. The three designs
// are drawn from Excel's gallery: Light15–21 (light header and grand total
// bands with rules — Light16 is Excel's default), Medium2 (solid header,
// banded subheadings) and Medium9 (tinted body, solid header and grand
// total). Excel draws the real style from the name; this is the screen's
// rendering of it.

#include "pivot_style.h"

#include <array>

namespace QindaOffice::Calc {

namespace {

enum Element {
    WholeTable, PageLabels, PageValues, ColStripe1, ColStripe2, RowStripe1, RowStripe2, FirstColumn, HeaderRow,
    FirstHeaderCell, SubtotalCol1, SubtotalCol2, SubtotalCol3, BlankRow, SubtotalRow1, SubtotalRow2, SubtotalRow3,
    ColSubheading1, ColSubheading2, ColSubheading3, RowSubheading1, RowSubheading2, RowSubheading3, GrandColumn,
    GrandRow, ElementCount
};

struct ElementFormat {
    std::optional<Rgb> fill;
    std::optional<Rgb> text;
    std::optional<bool> bold;
    std::optional<Rgb> top;
    std::optional<Rgb> bottom;
    bool topDouble = false;
};

using StyleDef = std::array<ElementFormat, ElementCount>;

// Excel's tint, on the RGB channels (lighter for t > 0, darker for t < 0).
Rgb tint(const Rgb &c, double t)
{
    const auto channel = [t](quint8 v) {
        const double x = t >= 0 ? v + (255.0 - v) * t : v * (1.0 + t);
        return quint8(qBound(0.0, x + 0.5, 255.0));
    };
    return Rgb{channel(c.r), channel(c.g), channel(c.b)};
}

ElementFormat bold()
{
    ElementFormat f;
    f.bold = true;
    return f;
}

StyleDef light(const Rgb &accent)
{
    StyleDef s;
    s[HeaderRow].fill = tint(accent, 0.8);
    s[HeaderRow].bold = true;
    s[HeaderRow].bottom = tint(accent, 0.4);
    s[GrandRow].fill = tint(accent, 0.8);
    s[GrandRow].bold = true;
    s[GrandRow].top = tint(accent, 0.4);
    s[RowStripe1].fill = tint(accent, 0.8);
    s[ColStripe1].fill = tint(accent, 0.8);
    for (const Element e : {RowSubheading1, RowSubheading2, SubtotalRow1, SubtotalRow2, SubtotalRow3, ColSubheading1,
                            ColSubheading2, SubtotalCol1, SubtotalCol2, GrandColumn, PageLabels}) {
        s[e] = bold();
    }
    s[SubtotalRow1].top = tint(accent, 0.4);
    return s;
}

StyleDef medium2(const Rgb &accent)
{
    StyleDef s;
    const Rgb white{255, 255, 255};
    s[HeaderRow].fill = accent;
    s[HeaderRow].text = white;
    s[HeaderRow].bold = true;
    s[PageLabels].fill = tint(accent, 0.8);
    s[PageLabels].bold = true;
    s[RowStripe1].fill = tint(accent, 0.8);
    s[ColStripe1].fill = tint(accent, 0.8);
    s[RowSubheading1].fill = tint(accent, 0.8);
    s[RowSubheading1].bold = true;
    s[RowSubheading2] = bold();
    s[SubtotalRow1] = bold();
    s[SubtotalRow1].top = tint(accent, 0.4);
    s[SubtotalRow2] = bold();
    s[ColSubheading1] = bold();
    s[SubtotalCol1] = bold();
    s[GrandColumn] = bold();
    s[GrandRow].fill = tint(accent, 0.8);
    s[GrandRow].bold = true;
    s[GrandRow].top = accent;
    s[GrandRow].topDouble = true;
    return s;
}

StyleDef medium9(const Rgb &accent)
{
    StyleDef s;
    const Rgb white{255, 255, 255};
    s[WholeTable].fill = tint(accent, 0.8);
    s[HeaderRow].fill = accent;
    s[HeaderRow].text = white;
    s[HeaderRow].bold = true;
    s[PageLabels].fill = accent;
    s[PageLabels].text = white;
    s[PageLabels].bold = true;
    s[RowStripe1].fill = tint(accent, 0.6);
    s[ColStripe1].fill = tint(accent, 0.6);
    s[RowSubheading1].fill = tint(accent, 0.6);
    s[RowSubheading1].bold = true;
    s[RowSubheading2] = bold();
    s[SubtotalRow1].fill = tint(accent, 0.6);
    s[SubtotalRow1].bold = true;
    s[SubtotalRow2] = bold();
    s[ColSubheading1] = bold();
    s[SubtotalCol1] = bold();
    s[GrandColumn].fill = tint(accent, 0.6);
    s[GrandColumn].bold = true;
    s[GrandRow].fill = accent;
    s[GrandRow].text = white;
    s[GrandRow].bold = true;
    return s;
}

std::optional<StyleDef> styleNamed(const QString &name, const QVector<Rgb> &theme)
{
    const auto color = [&theme](int index) {
        return index >= 0 && index < theme.size() ? theme.at(index) : Rgb{0x44, 0x72, 0xC4};
    };
    if (name.startsWith(QLatin1String("PivotStyleLight"))) {
        const int n = name.mid(15).toInt();
        // Light15 is the dark (text) variant; 16–21 take accents 1–6.
        if (n >= 15 && n <= 21) {
            return light(n == 15 ? color(0) : color(4 + (n - 16)));
        }
        return light(color(4));
    }
    if (name.startsWith(QLatin1String("PivotStyleMedium"))) {
        const int n = name.mid(16).toInt();
        // Medium1–7 and Medium8–14: the dark variant, then accents 1–6.
        const int variant = (n - 1) % 7;
        const Rgb accent = variant == 0 ? color(0) : color(4 + variant - 1);
        return n >= 8 ? medium9(accent) : medium2(accent);
    }
    if (name.startsWith(QLatin1String("PivotStyle"))) {
        return light(color(4)); // the other built-in families: drawn like the default
    }
    return std::nullopt;
}

Element level3(Element first, qint32 level)
{
    return Element(int(first) + qBound(0, int(level), 2));
}

} // namespace

QVector<Rgb> pivotDefaultTheme()
{
    return {Rgb{0, 0, 0}, Rgb{255, 255, 255}, Rgb{0x44, 0x54, 0x6A}, Rgb{0xE7, 0xE6, 0xE6}, Rgb{0x44, 0x72, 0xC4},
            Rgb{0xED, 0x7D, 0x31}, Rgb{0xA5, 0xA5, 0xA5}, Rgb{0xFF, 0xC0, 0x00}, Rgb{0x5B, 0x9B, 0xD5},
            Rgb{0x70, 0xAD, 0x47}};
}

QStringList pivotStyleNames()
{
    QStringList names;
    for (int n = 15; n <= 21; ++n) {
        names.append(QStringLiteral("PivotStyleLight%1").arg(n));
    }
    names << QStringLiteral("PivotStyleMedium2") << QStringLiteral("PivotStyleMedium9");
    return names;
}

std::optional<PivotCellLook> pivotCellLook(const PivotTable &pivot, const CellAddr &addr, const QVector<Rgb> &theme)
{
    const PivotRender *render = pivot.render.get();
    if (render == nullptr || !pivot.modelled || !render->extent.contains(addr)) {
        return std::nullopt;
    }
    PivotCellLook look;
    const PivotCell *cell = render->cellAt(addr);
    look.indent = cell != nullptr ? cell->indent : 0;
    const std::optional<StyleDef> style = styleNamed(pivot.options.style.name, theme);
    if (!style.has_value() || render->empty) {
        return look;
    }
    const PivotStyleInfo &info = pivot.options.style;
    std::array<bool, ElementCount> on{};
    on[WholeTable] = true;
    if (addr.row < render->body.topLeft.row) {
        if (cell != nullptr) {
            on[cell->kind == PivotCellKind::PageLabel ? PageLabels : PageValues] = true;
        }
    } else {
        const qint32 ro = addr.row - render->body.topLeft.row;
        const qint32 co = addr.col - render->body.topLeft.col;
        if (ro < render->firstDataRow) {
            on[HeaderRow] = info.colHeaders;
            const qint32 j = ro - render->firstHeaderRow;
            const qint32 levels = render->firstDataRow - render->firstHeaderRow;
            if (j >= 0 && j < levels - 1 && co >= render->firstDataCol) {
                on[level3(ColSubheading1, j)] = info.colHeaders;
            }
        } else if (const qsizetype ri = ro - render->firstDataRow; ri < render->rowLines.size()) {
            const PivotLine &line = render->rowLines.at(ri);
            switch (line.type) {
            case PivotLine::Type::Grand: on[GrandRow] = true; break;
            case PivotLine::Type::Subtotal: on[level3(SubtotalRow1, line.depth - 1)] = true; break;
            case PivotLine::Type::Blank: on[BlankRow] = true; break;
            case PivotLine::Type::Data:
                if (line.group) {
                    on[level3(RowSubheading1, line.depth - 1)] = info.rowHeaders;
                } else if (info.rowStripes) {
                    on[ri % 2 == 0 ? RowStripe1 : RowStripe2] = true;
                }
                break;
            }
        }
        if (co < render->firstDataCol) {
            on[FirstColumn] = info.rowHeaders;
        } else if (const qsizetype ci = co - render->firstDataCol; ci < render->colLines.size()) {
            const PivotLine &line = render->colLines.at(ci);
            if (line.type == PivotLine::Type::Grand) {
                on[GrandColumn] = info.lastColumn;
            } else if (line.type == PivotLine::Type::Subtotal) {
                on[level3(SubtotalCol1, line.depth - 1)] = true;
            } else if (info.colStripes && ro >= render->firstDataRow) {
                on[ci % 2 == 0 ? ColStripe1 : ColStripe2] = true;
            }
        }
    }
    for (int e = 0; e < ElementCount; ++e) {
        if (!on.at(e)) {
            continue;
        }
        const ElementFormat &f = style->at(e);
        if (f.fill) {
            look.fill = f.fill;
        }
        if (f.text) {
            look.text = f.text;
        }
        if (f.bold) {
            look.bold = *f.bold;
        }
        if (f.top) {
            look.top = f.top;
            look.topDouble = f.topDouble;
        }
        if (f.bottom) {
            look.bottom = f.bottom;
        }
    }
    return look;
}

} // namespace QindaOffice::Calc
