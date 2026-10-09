// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_borders.h"

#include "docx_format.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QStringList>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// Schema order shared by w:pBdr (top, left, bottom, right, between, bar),
// w:tcBorders (… insideH, insideV, tl2br, tr2bl) and w:tblBorders.
const QStringList &sideOrder()
{
    static const QStringList order{QStringLiteral("top"),     QStringLiteral("left"),    QStringLiteral("bottom"),
                                   QStringLiteral("right"),   QStringLiteral("insideH"), QStringLiteral("insideV"),
                                   QStringLiteral("tl2br"),   QStringLiteral("tr2bl"),   QStringLiteral("between"),
                                   QStringLiteral("bar")};
    return order;
}

QString sideName(const QString &name)
{
    return name == u"start" ? QStringLiteral("left") : name == u"end" ? QStringLiteral("right") : name;
}

const XmlElement *sourceSide(const XmlElement *source, const QString &side)
{
    if (source == nullptr) {
        return nullptr;
    }
    for (const XmlElement *e : source->elements()) {
        if (e->ns == kW && sideName(e->name) == side) {
            return e;
        }
    }
    return nullptr;
}

WordBorder normalised(WordBorder b)
{
    if (b.isNone()) {
        return WordBorder::none();
    }
    b.size = qBound(2, b.size, 96);
    b.color = b.color.isEmpty() || b.color.compare(u"auto", Qt::CaseInsensitive) == 0 ? QStringLiteral("auto")
                                                                                       : b.color.toUpper();
    return b;
}

} // namespace

QString bordersSpec(const WordBorders &borders)
{
    QStringList entries;
    for (auto it = borders.cbegin(); it != borders.cend(); ++it) {
        const WordBorder b = normalised(it.value());
        entries.append(QStringLiteral("%1=%2/%3/%4/%5").arg(it.key(), b.style).arg(b.size).arg(b.color).arg(b.space));
    }
    return entries.join(QLatin1Char(';'));
}

WordBorders parseBordersSpec(QStringView spec)
{
    WordBorders out;
    for (const QStringView entry : spec.split(u';', Qt::SkipEmptyParts)) {
        const qsizetype eq = entry.indexOf(u'=');
        if (eq <= 0) {
            continue;
        }
        const QList<QStringView> f = entry.mid(eq + 1).split(u'/');
        if (f.size() != 4) {
            continue;
        }
        WordBorder b{f[0].toString(), f[1].toInt(), f[2].toString(), f[3].toInt()};
        out.insert(entry.left(eq).toString(), normalised(b));
    }
    return out;
}

WordBorders readBorders(const XmlElement *element, const Ooxml::Theme &theme)
{
    WordBorders out;
    if (element == nullptr) {
        return out;
    }
    for (const XmlElement *side : element->elements()) {
        if (side->ns != kW) {
            continue;
        }
        WordBorder b;
        b.style = wAttr(side, u"val", QStringLiteral("single"));
        b.size = wAttr(side, u"sz", QStringLiteral("4")).toInt();
        const QColor color = wordColor(side, theme, u"color");
        b.color = color.isValid() ? color.name(QColor::HexRgb).mid(1).toUpper() : QStringLiteral("auto");
        b.space = qRound(wAttr(side, u"space", QStringLiteral("0")).toDouble());
        out.insert(sideName(side->name), normalised(b));
    }
    return out;
}

void writeBorders(XmlElement &container, const WordBorders &now, const WordBorders &then, const XmlElement *source)
{
    container.children.clear();
    for (const QString &side : sideOrder()) {
        const auto it = now.constFind(side);
        if (it == now.cend()) {
            continue;
        }
        const WordBorder b = normalised(it.value());
        const XmlElement *kept = then.contains(side) && normalised(then.value(side)) == b ? sourceSide(source, side)
                                                                                         : nullptr;
        if (kept != nullptr) {
            container.appendElement(kept->clone());
            continue;
        }
        XmlElement &e = container.appendElement(kW, side);
        e.setAttribute(kW, QStringLiteral("val"), b.style);
        if (b.isNone()) {
            continue;
        }
        e.setAttribute(kW, QStringLiteral("sz"), QString::number(b.size));
        e.setAttribute(kW, QStringLiteral("space"), QString::number(b.space));
        e.setAttribute(kW, QStringLiteral("color"), b.color);
    }
}

DisplayBorder displayBorder(const WordBorder &border)
{
    // AGENT-NOTE: the same mapping as the table reader always used
    // (docx_reader_table.cpp): Qt has solid, double, dotted, dashed and
    // dot-dash; Word's 3-D and multi-line styles take the nearest.
    DisplayBorder out;
    if (border.isNone()) {
        out.width = 0;
        return out;
    }
    const QString &val = border.style;
    out.width = qMax<qreal>(0.5, Units::ptToPx(border.size / 8.0));
    if (val == u"double" || val.startsWith(u"triple") || val.startsWith(u"thinThick") || val.startsWith(u"thickThin")) {
        out.style = QTextFrameFormat::BorderStyle_Double;
        out.width = qMax<qreal>(out.width, 2.5);
    } else if (val.startsWith(u"dot")) {
        out.style = val.startsWith(u"dotDash") ? QTextFrameFormat::BorderStyle_DotDash : QTextFrameFormat::BorderStyle_Dotted;
    } else if (val.startsWith(u"dash")) {
        out.style = QTextFrameFormat::BorderStyle_Dashed;
    }
    if (border.color != u"auto" && border.color.size() == 6) {
        out.color = QColor(QLatin1Char('#') + border.color);
    }
    return out;
}

QString paragraphDisplaySpec(const WordBorders &borders)
{
    QStringList sides;
    for (const QString &name : sideOrder()) {
        const auto it = borders.constFind(name);
        if (it == borders.cend() || it->isNone()
            || !(name == u"top" || name == u"bottom" || name == u"left" || name == u"right" || name == u"between")) {
            continue;
        }
        const WordBorder &b = it.value();
        // w:sz in eighths of a point, w:space in points.
        const qreal width = qMax<qreal>(0.75, Units::ptToPx(b.size / 8.0));
        const QString style = b.style.startsWith(u"double") ? QStringLiteral("double")
            : b.style.startsWith(u"dot")                    ? QStringLiteral("dot")
            : b.style.startsWith(u"dash")                   ? QStringLiteral("dash")
                                                            : QStringLiteral("solid");
        const QColor color = b.color == u"auto" ? QColor(Qt::black) : QColor(QLatin1Char('#') + b.color);
        sides.append(QStringLiteral("%1:%2:%3:%4:%5")
                         .arg(name)
                         .arg(width, 0, 'f', 2)
                         .arg(style, color.name())
                         .arg(Units::ptToPx(b.space), 0, 'f', 2));
    }
    return sides.join(QLatin1Char(';'));
}

QString marginsSpec(const CellMargins &m)
{
    return m.isEmpty() ? QString() : QStringLiteral("%1,%2,%3,%4").arg(m.top).arg(m.left).arg(m.bottom).arg(m.right);
}

CellMargins parseMarginsSpec(QStringView spec)
{
    const QList<QStringView> f = spec.split(u',');
    if (f.size() != 4) {
        return {};
    }
    return {f[0].toInt(), f[1].toInt(), f[2].toInt(), f[3].toInt()};
}

CellMargins readMargins(const XmlElement *element)
{
    CellMargins out;
    if (element == nullptr) {
        return out;
    }
    for (const XmlElement *side : element->elements()) {
        const QString type = wAttr(side, u"type", QStringLiteral("dxa"));
        if (side->ns != kW || (type != u"dxa" && type != u"nil")) {
            continue;
        }
        const int value = type == u"nil" ? 0 : qMax(0, qRound(Units::parseTwips(wAttr(side, u"w"))));
        const QString name = sideName(side->name);
        if (name == u"top") {
            out.top = value;
        } else if (name == u"left") {
            out.left = value;
        } else if (name == u"bottom") {
            out.bottom = value;
        } else if (name == u"right") {
            out.right = value;
        }
    }
    return out;
}

void writeMargins(XmlElement &container, const CellMargins &margins)
{
    container.children.clear();
    const std::pair<const char *, int> sides[] = {
        {"top", margins.top}, {"left", margins.left}, {"bottom", margins.bottom}, {"right", margins.right}};
    for (const auto &[name, value] : sides) {
        if (value < 0) {
            continue;
        }
        XmlElement &e = container.appendElement(kW, QString::fromLatin1(name));
        e.setAttribute(kW, QStringLiteral("w"), QString::number(value));
        e.setAttribute(kW, QStringLiteral("type"), QStringLiteral("dxa"));
    }
}

QString widthSpec(const WordWidth &width)
{
    return QStringLiteral("%1:%2").arg(width.type).arg(width.value);
}

WordWidth parseWidthSpec(QStringView spec)
{
    const qsizetype colon = spec.indexOf(u':');
    if (colon <= 0) {
        return {};
    }
    return {spec.left(colon).toString(), spec.mid(colon + 1).toInt()};
}

WordWidth readWidth(const XmlElement *element)
{
    if (element == nullptr) {
        return {};
    }
    WordWidth out;
    out.type = wAttr(element, u"type", QStringLiteral("dxa"));
    const QString w = wAttr(element, u"w", QStringLiteral("0"));
    if (w.endsWith(QLatin1Char('%'))) {
        out.type = QStringLiteral("pct"); // Strict: "50%"; Transitional: fiftieths
        out.value = qRound(w.chopped(1).toDouble() * 50);
    } else {
        out.value = qRound(Units::parseTwips(w));
    }
    return out;
}

void writeWidth(XmlElement &element, const WordWidth &width)
{
    element.attributes.clear();
    element.setAttribute(kW, QStringLiteral("w"), QString::number(width.type == u"auto" || width.type == u"nil" ? 0 : width.value));
    element.setAttribute(kW, QStringLiteral("type"), width.type);
}

} // namespace QindaOffice::Docx
