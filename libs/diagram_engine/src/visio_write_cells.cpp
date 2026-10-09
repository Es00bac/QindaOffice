// SPDX-License-Identifier: GPL-3.0-or-later
// Cells shared by written shapes and connectors (visio_write_cells.h).
#include "visio_write_cells.h"

#include "geometry.h"
#include "visio_text.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

QString n(double v)
{
    return numberText(v);
}

const QString kZero = QStringLiteral("0");

QString rowNameFor(const QString &key, const QSet<QString> &taken)
{
    QString name = key;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (name.isEmpty() || !name.at(0).isLetter()) {
        name.prepend(QStringLiteral("Row_"));
    }
    QString unique = name.left(48);
    for (int i = 2; taken.contains(unique.toLower()); ++i) {
        unique = name.left(44) + QLatin1Char('_') + QString::number(i);
    }
    return unique;
}

XmlElement *namedRow(XmlElement &section, const QString &name)
{
    for (Ooxml::XmlChild &c : section.children) {
        if (c.element && c.element->is(kNs, u"Row") && c.element->attribute({}, u"N") == name) {
            return c.element.get();
        }
    }
    return nullptr;
}

void propertyRow(XmlElement &section, const QString &name, const QString &label, const QString &value)
{
    XmlElement &row = appendElement(section, QStringLiteral("Row"));
    row.setAttribute({}, QStringLiteral("N"), name);
    appendCell(row, QStringLiteral("Value"), value, {}, QStringLiteral("STR"));
    appendCell(row, QStringLiteral("Prompt"), QString());
    appendCell(row, QStringLiteral("Label"), label);
    appendCell(row, QStringLiteral("Format"), QString());
    appendCell(row, QStringLiteral("SortKey"), QString());
    appendCell(row, QStringLiteral("Type"), kZero);
    appendCell(row, QStringLiteral("Invisible"), kZero);
    appendCell(row, QStringLiteral("Verify"), kZero);
    appendCell(row, QStringLiteral("DataLinked"), kZero);
    appendCell(row, QStringLiteral("LangID"), QStringLiteral("en-US"));
    appendCell(row, QStringLiteral("Calendar"), kZero);
}

} // namespace

double pxPerInch(const QTransform &toParent)
{
    return 1.0 / std::max(1e-12, std::hypot(toParent.m11(), toParent.m12()));
}

Placement placementOf(const Shape &shape, const QTransform &toParent)
{
    Placement p;
    p.pxPerInch = pxPerInch(toParent);
    p.width = shape.rect.width() / p.pxPerInch;
    p.height = shape.rect.height() / p.pxPerInch;
    // Visio's local space (inches, y up) → the model's (page units, y down)
    // → the page → the parent: its linear part is a flip and a turn.
    const QTransform local(p.pxPerInch, 0, 0, -p.pxPerInch, 0, p.height * p.pxPerInch);
    const QTransform total = local * shapeTransform(shape) * toParent;
    const QPointF pin = toParent.map(shape.rect.center());
    double m11 = total.m11();
    double m12 = total.m12();
    p.flipX = m11 * total.m22() - m12 * total.m21() < 0;
    if (p.flipX) {
        m11 = -m11;
        m12 = -m12;
    }
    p.angle = std::atan2(m12, m11);
    if (std::abs(p.angle) < 1e-9) {
        p.angle = 0;
    }
    p.pinX = pin.x();
    p.pinY = pin.y();
    return p;
}

void writePlacement(XmlElement &shape, const Placement &p)
{
    setCell(shape, QStringLiteral("PinX"), n(p.pinX));
    setCell(shape, QStringLiteral("PinY"), n(p.pinY));
    setCell(shape, QStringLiteral("Width"), n(p.width));
    setCell(shape, QStringLiteral("Height"), n(p.height));
    setCell(shape, QStringLiteral("LocPinX"), n(p.width / 2), QStringLiteral("Width*0.5"));
    setCell(shape, QStringLiteral("LocPinY"), n(p.height / 2), QStringLiteral("Height*0.5"));
    setCell(shape, QStringLiteral("Angle"), n(p.angle));
    setCell(shape, QStringLiteral("FlipX"), p.flipX ? QStringLiteral("1") : kZero);
    setCell(shape, QStringLiteral("FlipY"), kZero);
}

void writeLine(XmlElement &shape, const QColor &color, double widthPx, LinePattern pattern, double opacity)
{
    const bool visible = color.isValid() && color.alpha() > 0 && widthPx > 0;
    const int visio = pattern == LinePattern::Dash ? 2 : pattern == LinePattern::Dot ? 3 : pattern == LinePattern::DashDot ? 4 : 1;
    // Line weights are paper sizes: never scaled with the drawing.
    setCell(shape, QStringLiteral("LineWeight"), n(std::max(0.0, widthPx) / kPx), {}, QStringLiteral("PT"));
    setCell(shape, QStringLiteral("LinePattern"), visible ? QString::number(visio) : kZero);
    if (visible) {
        setCell(shape, QStringLiteral("LineColor"), colorText(color));
        setCell(shape, QStringLiteral("LineColorTrans"), n(1.0 - std::clamp(color.alphaF() * opacity, 0.0, 1.0)));
    }
    setCell(shape, QStringLiteral("LineGradientEnabled"), kZero);
}

void writeFill(XmlElement &shape, const ShapeStyle &style)
{
    const double opacity = std::clamp(style.opacity, 0.0, 1.0);
    const bool visible = style.fill.isValid() && style.fill.alpha() > 0;
    const bool gradient = visible && style.gradient.isValid() && style.gradient.alpha() > 0;
    if (visible) {
        setCell(shape, QStringLiteral("FillForegnd"), colorText(style.fill));
        setCell(shape, QStringLiteral("FillForegndTrans"), n(1.0 - style.fill.alphaF() * opacity));
        const QColor bottom = gradient ? style.gradient : style.fill;
        setCell(shape, QStringLiteral("FillBkgnd"), colorText(bottom));
        setCell(shape, QStringLiteral("FillBkgndTrans"), n(1.0 - bottom.alphaF() * opacity));
    }
    // 28: the legacy linear gradient, foreground at the top — the one
    // gradient every Visio and LibreOffice draws (D-073).
    setCell(shape, QStringLiteral("FillPattern"), !visible ? kZero : gradient ? QStringLiteral("28") : QStringLiteral("1"));
    setCell(shape, QStringLiteral("FillGradientEnabled"), kZero);
    setCell(shape, QStringLiteral("ShdwPattern"), style.shadow ? QStringLiteral("1") : kZero);
    if (style.shadow) {
        setCell(shape, QStringLiteral("ShdwForegnd"), QStringLiteral("#0f172a"));
        setCell(shape, QStringLiteral("ShdwForegndTrans"), QStringLiteral("0.85"));
        setCell(shape, QStringLiteral("ShapeShdwType"), QStringLiteral("1"));
        setCell(shape, QStringLiteral("ShapeShdwOffsetX"), n(2.0 / kPx));
        setCell(shape, QStringLiteral("ShapeShdwOffsetY"), n(-3.0 / kPx));
    }
}

int visioArrow(ArrowStyle style)
{
    switch (style) {
    case ArrowStyle::None:
        return 0;
    case ArrowStyle::Open:
        return 1;
    case ArrowStyle::Filled:
        return 4;
    case ArrowStyle::Diamond:
        return 22;
    case ArrowStyle::Circle:
        return 10;
    }
    return 0;
}

void writeTextBlock(XmlElement &shape, const QRectF &box, const Placement &p, const TextStyle &style)
{
    const QPointF c = box.center();
    setCell(shape, QStringLiteral("TxtPinX"), n(c.x() * p.width), QStringLiteral("Width*") + n(c.x()));
    setCell(shape, QStringLiteral("TxtPinY"), n((1 - c.y()) * p.height), QStringLiteral("Height*") + n(1 - c.y()));
    setCell(shape, QStringLiteral("TxtWidth"), n(box.width() * p.width), QStringLiteral("Width*") + n(box.width()));
    setCell(shape, QStringLiteral("TxtHeight"), n(box.height() * p.height), QStringLiteral("Height*") + n(box.height()));
    setCell(shape, QStringLiteral("TxtLocPinX"), n(box.width() * p.width / 2), QStringLiteral("TxtWidth*0.5"));
    setCell(shape, QStringLiteral("TxtLocPinY"), n(box.height() * p.height / 2), QStringLiteral("TxtHeight*0.5"));
    setCell(shape, QStringLiteral("TxtAngle"), kZero);
    for (const char *margin : {"LeftMargin", "RightMargin", "TopMargin", "BottomMargin"}) {
        setCell(shape, QString::fromLatin1(margin), kZero, {}, QStringLiteral("PT"));
    }
    setCell(shape, QStringLiteral("VerticalAlign"), verticalAlignValue(style));
}

void insertChild(XmlElement &shape, XmlElementPtr element)
{
    shape.insertElementBefore(std::move(element), {u"Text", u"Data1", u"Data2", u"Data3", u"ForeignData", u"Shapes"});
}

void writeText(XmlElement &shape, const QString &text, const TextStyle &style, const QString &family)
{
    removeSections(shape, u"Character");
    removeSections(shape, u"Paragraph");
    shape.removeChildren(kNs, u"Text");
    insertChild(shape, characterSection(style, family));
    insertChild(shape, paragraphSection(style));
    if (text.isEmpty()) {
        // An empty <Text/> hides text a master would otherwise lend.
        insertChild(shape, makeElement(QStringLiteral("Text")));
    } else {
        insertChild(shape, textElement(text));
    }
}

XmlElementPtr dataSection(const QList<DataField> &data)
{
    XmlElementPtr section = makeElement(QStringLiteral("Section"));
    section->setAttribute({}, QStringLiteral("N"), QStringLiteral("Property"));
    QSet<QString> taken;
    for (const DataField &field : data) {
        const QString name = rowNameFor(field.key, taken);
        taken.insert(name.toLower());
        propertyRow(*section, name, field.key, field.value);
    }
    return section;
}

void updateData(XmlElement &shape, const QList<DataField> &before, const QList<DataField> &after, const Sheet &merged,
                const Sheet *master)
{
    // The row behind each visible key, the way the reader named them.
    QHash<QString, QString> rows;
    QSet<QString> taken;
    if (const Section *s = merged.section(u"Property")) {
        for (const auto &[name, row] : s->namedRows()) {
            taken.insert(name.toLower());
            if (row->number(u"Invisible", 0) != 0) {
                continue;
            }
            const Cell *label = row->cell(u"Label");
            QString key = label != nullptr && !label->value.trimmed().isEmpty() ? label->value.trimmed() : name;
            if (rows.contains(key.toLower())) {
                key = QStringLiteral("%1 (%2)").arg(key, name);
            }
            rows.insert(key.toLower(), name);
        }
    }
    XmlElement *section = Visio::section(shape, u"Property");
    if (section == nullptr) {
        XmlElementPtr fresh = makeElement(QStringLiteral("Section"));
        fresh->setAttribute({}, QStringLiteral("N"), QStringLiteral("Property"));
        section = fresh.get();
        insertChild(shape, fresh);
    }
    const Section *inherited = master != nullptr ? master->section(u"Property") : nullptr;
    for (const DataField &old : before) {
        const bool kept = std::any_of(after.cbegin(), after.cend(),
                                      [&](const DataField &f) { return f.key.compare(old.key, Qt::CaseInsensitive) == 0; });
        const QString name = rows.value(old.key.toLower());
        if (kept || name.isEmpty()) {
            continue;
        }
        section->children.removeIf([&](const Ooxml::XmlChild &c) {
            return c.element && c.element->is(kNs, u"Row") && c.element->attribute({}, u"N") == name;
        });
        if (inherited != nullptr && inherited->rows.contains(QStringLiteral("N:") + name)) {
            XmlElement &del = appendElement(*section, QStringLiteral("Row"));
            del.setAttribute({}, QStringLiteral("N"), name);
            del.setAttribute({}, QStringLiteral("Del"), QStringLiteral("1"));
        }
    }
    for (const DataField &field : after) {
        const auto old = std::find_if(before.cbegin(), before.cend(),
                                      [&](const DataField &f) { return f.key.compare(field.key, Qt::CaseInsensitive) == 0; });
        if (old != before.cend() && old->value == field.value) {
            continue;
        }
        const QString name = rows.value(field.key.toLower());
        if (name.isEmpty()) {
            const QString fresh = rowNameFor(field.key, taken);
            taken.insert(fresh.toLower());
            propertyRow(*section, fresh, field.key, field.value);
            continue;
        }
        XmlElement *row = namedRow(*section, name);
        if (row == nullptr) {
            row = &appendElement(*section, QStringLiteral("Row"));
            row->setAttribute({}, QStringLiteral("N"), name);
        }
        setCell(*row, QStringLiteral("Value"), field.value, {}, QStringLiteral("STR"));
    }
}

XmlElementPtr connectionSection(const QList<QPointF> &points, double width, double height)
{
    XmlElementPtr section = makeElement(QStringLiteral("Section"));
    section->setAttribute({}, QStringLiteral("N"), QStringLiteral("Connection"));
    int ix = 0;
    for (const QPointF &p : points) {
        XmlElement &row = appendElement(*section, QStringLiteral("Row"));
        row.setAttribute({}, QStringLiteral("T"), QStringLiteral("Connection"));
        row.setAttribute({}, QStringLiteral("IX"), QString::number(ix++));
        // The formulas keep the point on the shape at any size.
        appendCell(row, QStringLiteral("X"), n(p.x() * width), QStringLiteral("Width*") + n(p.x()));
        appendCell(row, QStringLiteral("Y"), n((1 - p.y()) * height), QStringLiteral("Height*") + n(1 - p.y()));
        appendCell(row, QStringLiteral("DirX"), kZero);
        appendCell(row, QStringLiteral("DirY"), kZero);
        appendCell(row, QStringLiteral("Type"), kZero);
        appendCell(row, QStringLiteral("AutoGen"), kZero);
    }
    return section;
}

int clearGeometry(XmlElement &shape, const XmlElement *master)
{
    removeSections(shape, u"Geometry");
    int next = 0;
    if (master == nullptr) {
        return next;
    }
    for (const XmlElement *s : children(*master, u"Section")) {
        if (s->attribute({}, u"N") != QLatin1String("Geometry")) {
            continue;
        }
        const int ix = intAttribute(*s, u"IX", 0);
        next = std::max(next, ix + 1);
        XmlElementPtr hidden = makeElement(QStringLiteral("Section"));
        hidden->setAttribute({}, QStringLiteral("N"), QStringLiteral("Geometry"));
        hidden->setAttribute({}, QStringLiteral("IX"), QString::number(ix));
        appendCell(*hidden, QStringLiteral("NoShow"), QStringLiteral("1"));
        insertChild(shape, hidden);
    }
    return next;
}

} // namespace QindaOffice::Diagram::Visio
