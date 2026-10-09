// SPDX-License-Identifier: GPL-3.0-or-later
// Shape text (visio_text.h).
#include "visio_text.h"

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

QString normalized(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x2028), QLatin1Char('\n'));
    text.replace(QChar(0x2029), QLatin1Char('\n'));
    return text;
}

void collect(const XmlElement &element, ParsedText &out, int &cp, int &pp)
{
    for (const Ooxml::XmlChild &c : element.children) {
        if (!c.element) {
            const QString piece = normalized(c.text);
            if (!piece.isEmpty()) {
                out.runs.append({int(out.text.size()), int(piece.size()), cp, pp});
                out.text += piece;
            }
            continue;
        }
        const XmlElement &e = *c.element;
        if (e.is(kNs, u"cp")) {
            cp = intAttribute(e, u"IX", 0);
        } else if (e.is(kNs, u"pp")) {
            pp = intAttribute(e, u"IX", 0);
        } else if (e.is(kNs, u"fld")) {
            out.fields = true;
            collect(e, out, cp, pp);
        }
        // <tp> switches tab stops, which the model does not lay out.
    }
}

} // namespace

ParsedText parseText(const XmlElement &text)
{
    ParsedText out;
    int cp = 0;
    int pp = 0;
    collect(text, out, cp, pp);
    if (out.text.endsWith(QLatin1Char('\n'))) {
        out.text.chop(1);
        if (!out.runs.isEmpty()) {
            TextRun &last = out.runs.last();
            last.length = std::max(0, last.length - 1);
        }
    }
    return out;
}

TextRun leadingRun(const ParsedText &text)
{
    for (const TextRun &run : text.runs) {
        if (!text.text.mid(run.start, run.length).trimmed().isEmpty()) {
            return run;
        }
    }
    return text.runs.isEmpty() ? TextRun() : text.runs.first();
}

TextStyle readTextStyle(const Lookup &lookup, int charIx, int paraIx)
{
    TextStyle style;
    const auto charCell = [&](const char16_t *name) { return lookup.textCell(u"Character", charIx, name); };
    style.family = lookup.fontName(charCell(u"Font"));
    if (const Cell *size = charCell(u"Size")) {
        style.size = std::clamp(size->number().value_or(1.0 / 6.0) * 72.0, 1.0, 400.0);
    } else {
        style.size = 12;
    }
    const int bits = charCell(u"Style") != nullptr && !charCell(u"Style")->themed()
                         ? int(charCell(u"Style")->number().value_or(0))
                         : 0;
    style.bold = bits & 1;
    style.italic = bits & 2;
    style.underline = (bits & 4) || (charCell(u"DblUnderline") != nullptr && charCell(u"DblUnderline")->value == u"1");
    style.color = lookup.colorOf(charCell(u"Color"), ColorRole::Font);
    if (const Cell *trans = charCell(u"ColorTrans"); trans != nullptr && style.color.isValid()) {
        style.color.setAlphaF(float(std::clamp(1.0 - trans->number().value_or(0), 0.0, 1.0)));
    }
    Qt::Alignment h = Qt::AlignHCenter;
    if (const Cell *align = lookup.textCell(u"Paragraph", paraIx, u"HorzAlign")) {
        const int a = int(align->number().value_or(1));
        h = a == 0 || a >= 3 ? Qt::AlignLeft : a == 2 ? Qt::AlignRight : Qt::AlignHCenter;
    }
    Qt::Alignment v = Qt::AlignVCenter;
    const int vertical = int(lookup.number(u"VerticalAlign", 1));
    v = vertical == 0 ? Qt::AlignTop : vertical == 2 ? Qt::AlignBottom : Qt::AlignVCenter;
    style.align = h | v;
    return style;
}

XmlElementPtr textElement(const QString &text)
{
    XmlElementPtr e = makeElement(QStringLiteral("Text"));
    appendElement(*e, QStringLiteral("cp")).setAttribute({}, QStringLiteral("IX"), QStringLiteral("0"));
    appendElement(*e, QStringLiteral("pp")).setAttribute({}, QStringLiteral("IX"), QStringLiteral("0"));
    e->appendText(text + QLatin1Char('\n'));
    return e;
}

XmlElementPtr characterSection(const TextStyle &style, const QString &family)
{
    XmlElementPtr s = makeElement(QStringLiteral("Section"));
    s->setAttribute({}, QStringLiteral("N"), QStringLiteral("Character"));
    XmlElement &row = appendElement(*s, QStringLiteral("Row"));
    row.setAttribute({}, QStringLiteral("IX"), QStringLiteral("0"));
    appendCell(row, QStringLiteral("Font"), family);
    if (style.color.isValid()) {
        appendCell(row, QStringLiteral("Color"), colorText(style.color));
    }
    appendCell(row, QStringLiteral("Style"),
               QString::number((style.bold ? 1 : 0) | (style.italic ? 2 : 0) | (style.underline ? 4 : 0)));
    appendCell(row, QStringLiteral("Size"), numberText(style.size / 72.0), {}, QStringLiteral("PT"));
    if (style.color.isValid() && style.color.alpha() < 255) {
        appendCell(row, QStringLiteral("ColorTrans"), numberText(1.0 - style.color.alphaF()));
    }
    return s;
}

XmlElementPtr paragraphSection(const TextStyle &style)
{
    XmlElementPtr s = makeElement(QStringLiteral("Section"));
    s->setAttribute({}, QStringLiteral("N"), QStringLiteral("Paragraph"));
    XmlElement &row = appendElement(*s, QStringLiteral("Row"));
    row.setAttribute({}, QStringLiteral("IX"), QStringLiteral("0"));
    const QString align = style.align & Qt::AlignLeft    ? QStringLiteral("0")
                        : style.align & Qt::AlignRight   ? QStringLiteral("2")
                                                         : QStringLiteral("1");
    appendCell(row, QStringLiteral("HorzAlign"), align);
    return s;
}

QString verticalAlignValue(const TextStyle &style)
{
    return style.align & Qt::AlignTop      ? QStringLiteral("0")
         : style.align & Qt::AlignBottom   ? QStringLiteral("2")
                                           : QStringLiteral("1");
}

} // namespace QindaOffice::Diagram::Visio
