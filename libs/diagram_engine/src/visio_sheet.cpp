// SPDX-License-Identifier: GPL-3.0-or-later
// ShapeSheet inheritance (visio_sheet.h).
#include "visio_sheet.h"

#include "ooxml_units.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

bool deleted(const XmlElement &e)
{
    return Ooxml::Units::parseOnOff(e.attribute({}, u"Del"), false);
}

QString indexKey(int ix)
{
    return QStringLiteral("%1").arg(ix, 6, 10, QLatin1Char('0'));
}

QString rowKey(const XmlElement &row)
{
    const QString name = row.attribute({}, u"N");
    return name.isEmpty() ? indexKey(intAttribute(row, u"IX", 0)) : QStringLiteral("N:") + name;
}

QString sectionKey(const XmlElement &section)
{
    const QString name = section.attribute({}, u"N");
    // Only Geometry repeats within one shape; its sections are told apart
    // (and inherited) by IX.
    return name == QLatin1String("Geometry") ? name + QLatin1Char('#') + indexKey(intAttribute(section, u"IX", 0))
                                             : name;
}

// Overlays the <Cell> children of `from` on `cells`: a cell without V keeps
// the inherited value (an F-only override still shows the old value).
void mergeCells(const XmlElement &from, QHash<QString, Cell> &cells)
{
    for (const XmlElement *c : children(from, u"Cell")) {
        if (!c->hasAttribute({}, u"V")) {
            continue;
        }
        cells.insert(c->attribute({}, u"N"),
                     Cell{c->attribute({}, u"V"), c->attribute({}, u"F"), c->attribute({}, u"U")});
    }
}

void mergeSection(const XmlElement &own, Section &section)
{
    mergeCells(own, section.cells);
    for (const XmlElement *r : children(own, u"Row")) {
        const QString key = rowKey(*r);
        if (deleted(*r)) {
            section.rows.remove(key);
            section.order.removeAll(key);
            continue;
        }
        if (!section.rows.contains(key)) {
            section.order.append(key);
        }
        Row &row = section.rows[key];
        const QString type = r->attribute({}, u"T");
        if (!type.isEmpty()) {
            row.type = type;
        }
        mergeCells(*r, row.cells);
    }
    // Indexed rows run in IX order whatever order they were met in.
    const bool indexed = std::all_of(section.order.cbegin(), section.order.cend(),
                                     [](const QString &k) { return !k.startsWith(QLatin1String("N:")); });
    if (indexed) {
        std::sort(section.order.begin(), section.order.end());
    }
}

int styleAttribute(const XmlElement &e, QStringView name, int inherited)
{
    return intAttribute(e, name, inherited);
}

} // namespace

std::optional<double> Cell::number() const
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (ok && std::isfinite(v)) {
        return v;
    }
    return std::nullopt;
}

const Cell *Row::cell(QStringView name) const
{
    const auto it = cells.constFind(name.toString());
    return it == cells.constEnd() ? nullptr : &*it;
}

double Row::number(QStringView name, double fallback) const
{
    const Cell *c = cell(name);
    return c != nullptr ? c->number().value_or(fallback) : fallback;
}

const Row *Section::indexed(int ix) const
{
    const auto it = rows.constFind(indexKey(ix));
    return it == rows.constEnd() ? nullptr : &*it;
}

QList<const Row *> Section::orderedRows() const
{
    QList<const Row *> out;
    for (const QString &key : order) {
        const auto it = rows.constFind(key);
        if (it != rows.constEnd()) {
            out.append(&*it);
        }
    }
    return out;
}

QList<QPair<QString, const Row *>> Section::namedRows() const
{
    QList<QPair<QString, const Row *>> out;
    for (const QString &key : order) {
        const auto it = rows.constFind(key);
        if (key.startsWith(QLatin1String("N:")) && it != rows.constEnd()) {
            out.append({key.mid(2), &*it});
        }
    }
    return out;
}

bool Section::flag(QStringView cellName) const
{
    const auto it = cells.constFind(cellName.toString());
    return it != cells.constEnd() && it->number().value_or(0) != 0;
}

Sheet Sheet::merge(const XmlElement &own, const Sheet *base)
{
    Sheet s = base != nullptr ? *base : Sheet();
    s.lineStyle = styleAttribute(own, u"LineStyle", s.lineStyle);
    s.fillStyle = styleAttribute(own, u"FillStyle", s.fillStyle);
    s.textStyle = styleAttribute(own, u"TextStyle", s.textStyle);
    const QString type = own.attribute({}, u"Type");
    if (!type.isEmpty()) {
        s.type = type;
    }
    mergeCells(own, s.m_cells);
    for (const XmlElement *e : own.elements()) {
        if (e->is(kNs, u"Section")) {
            const QString key = sectionKey(*e);
            if (deleted(*e)) {
                s.m_sections.remove(key);
                continue;
            }
            Section &section = s.m_sections[key];
            section.name = e->attribute({}, u"N");
            section.index = intAttribute(*e, u"IX", 0);
            mergeSection(*e, section);
        } else if (e->is(kNs, u"Text")) {
            s.m_text = e;
        } else if (e->is(kNs, u"ForeignData")) {
            s.m_foreign = e;
        }
    }
    return s;
}

const Cell *Sheet::cell(QStringView name) const
{
    const auto it = m_cells.constFind(name.toString());
    return it == m_cells.constEnd() ? nullptr : &*it;
}

const Section *Sheet::section(QStringView name) const
{
    for (const Section &s : m_sections) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

QList<const Section *> Sheet::sections(QStringView name) const
{
    QList<const Section *> out;
    for (const Section &s : m_sections) { // QMap: ordered by key, so by IX
        if (s.name == name) {
            out.append(&s);
        }
    }
    return out;
}

Category categoryOf(QStringView name)
{
    static const QSet<QString> line{
        QStringLiteral("LineWeight"),        QStringLiteral("LineColor"),           QStringLiteral("LinePattern"),
        QStringLiteral("Rounding"),          QStringLiteral("EndArrowSize"),        QStringLiteral("BeginArrow"),
        QStringLiteral("EndArrow"),          QStringLiteral("LineCap"),             QStringLiteral("BeginArrowSize"),
        QStringLiteral("LineColorTrans"),    QStringLiteral("CompoundType"),        QStringLiteral("QuickStyleLineColor"),
        QStringLiteral("QuickStyleLineMatrix"), QStringLiteral("LineGradientDir"), QStringLiteral("LineGradientAngle"),
        QStringLiteral("LineGradientEnabled")};
    static const QSet<QString> fill{
        QStringLiteral("FillForegnd"),        QStringLiteral("FillBkgnd"),          QStringLiteral("FillPattern"),
        QStringLiteral("ShdwForegnd"),        QStringLiteral("ShdwBkgnd"),          QStringLiteral("ShdwPattern"),
        QStringLiteral("FillForegndTrans"),   QStringLiteral("FillBkgndTrans"),     QStringLiteral("ShdwForegndTrans"),
        QStringLiteral("ShdwBkgndTrans"),     QStringLiteral("ShapeShdwType"),      QStringLiteral("ShapeShdwOffsetX"),
        QStringLiteral("ShapeShdwOffsetY"),   QStringLiteral("ShapeShdwObliqueAngle"),
        QStringLiteral("ShapeShdwScaleFactor"), QStringLiteral("ShapeShdwBlur"),    QStringLiteral("ShapeShdwShow"),
        QStringLiteral("QuickStyleFillColor"), QStringLiteral("QuickStyleFillMatrix"),
        QStringLiteral("QuickStyleShadowColor"), QStringLiteral("QuickStyleEffectsMatrix"),
        QStringLiteral("FillGradientDir"),    QStringLiteral("FillGradientAngle"),  QStringLiteral("FillGradientEnabled"),
        QStringLiteral("RotateGradientWithShape"), QStringLiteral("UseGroupGradient")};
    static const QSet<QString> text{
        QStringLiteral("LeftMargin"),     QStringLiteral("RightMargin"),    QStringLiteral("TopMargin"),
        QStringLiteral("BottomMargin"),   QStringLiteral("VerticalAlign"),  QStringLiteral("TextBkgnd"),
        QStringLiteral("DefaultTabStop"), QStringLiteral("TextDirection"),  QStringLiteral("TextBkgndTrans"),
        QStringLiteral("QuickStyleFontColor"), QStringLiteral("QuickStyleFontMatrix")};
    const QString key = name.toString();
    if (line.contains(key)) {
        return Category::Line;
    }
    if (fill.contains(key)) {
        return Category::Fill;
    }
    if (text.contains(key)) {
        return Category::Text;
    }
    return Category::Other;
}

void Styles::load(const XmlElement *styleSheets)
{
    m_styles.clear();
    if (styleSheets == nullptr) {
        return;
    }
    for (const XmlElement *e : children(*styleSheets, u"StyleSheet")) {
        Entry entry;
        entry.sheet = Sheet::merge(*e, nullptr);
        entry.parent[0] = entry.sheet.lineStyle;
        entry.parent[1] = entry.sheet.fillStyle;
        entry.parent[2] = entry.sheet.textStyle;
        entry.nameU = e->attribute({}, u"NameU", e->attribute({}, u"Name"));
        m_styles.insert(intAttribute(*e, u"ID", -1), entry);
    }
}

const Cell *Styles::cell(int id, Category category, QStringView name) const
{
    // AGENT-GUARD: a chain is at most 64 long so a style naming itself (or a
    // loop through two styles) in a hostile file cannot hang the reader.
    for (int depth = 0; depth < 64 && m_styles.contains(id); ++depth) {
        const Entry &e = *m_styles.constFind(id);
        if (const Cell *c = e.sheet.cell(name)) {
            return c;
        }
        const int next = category == Category::Line   ? e.parent[0]
                       : category == Category::Fill   ? e.parent[1]
                       : category == Category::Text   ? e.parent[2]
                       : e.parent[0] >= 0             ? e.parent[0]
                                                      : e.parent[1];
        if (next == id) {
            break;
        }
        id = next;
    }
    return nullptr;
}

const Cell *Styles::rowCell(int id, QStringView sectionName, int ix, QStringView name) const
{
    for (int depth = 0; depth < 64 && m_styles.contains(id); ++depth) {
        const Entry &e = *m_styles.constFind(id);
        if (const Section *s = e.sheet.section(sectionName)) {
            if (const Row *r = s->indexed(ix)) {
                if (const Cell *c = r->cell(name)) {
                    return c;
                }
            }
        }
        if (e.parent[2] == id) {
            break;
        }
        id = e.parent[2];
    }
    return nullptr;
}

int Styles::idNamed(QStringView nameU) const
{
    for (auto it = m_styles.cbegin(); it != m_styles.cend(); ++it) {
        if (it->nameU.compare(nameU, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return -1;
}

} // namespace QindaOffice::Diagram::Visio
