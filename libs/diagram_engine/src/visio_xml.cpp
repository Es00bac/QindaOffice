// SPDX-License-Identifier: GPL-3.0-or-later
// VSDX vocabulary helpers (visio_xml.h).
#include "visio_xml.h"

#include <QColor>

#include <cmath>

namespace QindaOffice::Diagram::Visio {

using Ooxml::XmlChild;

namespace {

QString ns()
{
    return QString::fromUtf16(kNs);
}

XmlElement *findCell(XmlElement &parent, QStringView name)
{
    for (XmlChild &c : parent.children) {
        if (c.element && c.element->is(kNs, u"Cell") && c.element->attribute({}, u"N") == name) {
            return c.element.get();
        }
    }
    return nullptr;
}

void fillCell(XmlElement &cell, const QString &value, const QString &formula, const QString &unit)
{
    cell.attributes.clear();
    cell.children.clear();
    cell.attributes.append({QString(), QStringLiteral("N"), QString()}); // value set by the caller
    cell.attributes.append({QString(), QStringLiteral("V"), value});
    if (!unit.isEmpty()) {
        cell.attributes.append({QString(), QStringLiteral("U"), unit});
    }
    if (!formula.isEmpty()) {
        cell.attributes.append({QString(), QStringLiteral("F"), formula});
    }
}

} // namespace

const XmlElement *child(const XmlElement &e, QStringView name)
{
    return e.child(kNs, name);
}

XmlElement *child(XmlElement &e, QStringView name)
{
    return e.child(kNs, name);
}

QList<const XmlElement *> children(const XmlElement &e, QStringView name)
{
    return e.elements(kNs, name);
}

XmlElementPtr makeElement(const QString &name)
{
    return std::make_shared<XmlElement>(ns(), name);
}

XmlElement &appendElement(XmlElement &parent, const QString &name)
{
    return parent.appendElement(ns(), name);
}

int intAttribute(const XmlElement &e, QStringView name, int fallback)
{
    bool ok = false;
    const int value = e.attribute({}, name).toInt(&ok);
    return ok ? value : fallback;
}

QString numberText(double value)
{
    if (!std::isfinite(value) || std::abs(value) < 1e-12) {
        return QStringLiteral("0");
    }
    QString text = QString::number(value, 'g', 16);
    if (text.contains(QLatin1Char('e'))) {
        // Visio writes plain decimals; keep sub-micro-inch values readable.
        text = QString::number(value, 'f', 12);
        while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
            const bool dot = text.endsWith(QLatin1Char('.'));
            text.chop(1);
            if (dot) {
                break;
            }
        }
        if (text == QLatin1String("-0") || text.isEmpty()) {
            text = QStringLiteral("0");
        }
    }
    return text;
}

XmlElement &appendCell(XmlElement &parent, const QString &name, const QString &value, const QString &formula,
                       const QString &unit)
{
    XmlElement &cell = appendElement(parent, QStringLiteral("Cell"));
    fillCell(cell, value, formula, unit);
    cell.attributes[0].value = name;
    return cell;
}

XmlElement &setCell(XmlElement &parent, const QString &name, const QString &value, const QString &formula,
                    const QString &unit)
{
    if (XmlElement *cell = findCell(parent, name)) {
        fillCell(*cell, value, formula, unit);
        cell->attributes[0].value = name;
        return *cell;
    }
    // A new cell goes before the first section, row or text so the cells
    // stay together the way Visio writes them.
    auto element = makeElement(QStringLiteral("Cell"));
    fillCell(*element, value, formula, unit);
    element->attributes[0].value = name;
    return parent.insertElementBefore(element, {u"Trigger", u"Section", u"Row", u"Text", u"Data1", u"Data2",
                                                u"Data3", u"ForeignData", u"Shapes"});
}

void removeCell(XmlElement &parent, QStringView name)
{
    parent.children.removeIf([&](const XmlChild &c) {
        return c.element && c.element->is(kNs, u"Cell") && c.element->attribute({}, u"N") == name;
    });
}

XmlElement *section(XmlElement &shape, QStringView name, int ix)
{
    for (XmlChild &c : shape.children) {
        if (!c.element || !c.element->is(kNs, u"Section") || c.element->attribute({}, u"N") != name) {
            continue;
        }
        if (ix < 0 || intAttribute(*c.element, u"IX", 0) == ix) {
            return c.element.get();
        }
    }
    return nullptr;
}

void removeSections(XmlElement &shape, QStringView name)
{
    shape.children.removeIf([&](const XmlChild &c) {
        return c.element && c.element->is(kNs, u"Section") && c.element->attribute({}, u"N") == name;
    });
}

QString colorText(const QColor &color)
{
    return color.isValid() ? color.name(QColor::HexRgb).toLower() : QStringLiteral("#000000");
}

} // namespace QindaOffice::Diagram::Visio
