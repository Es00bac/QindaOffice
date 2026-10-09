// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_styles.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// Revision history and section breaks are not inheritable properties.
bool isNotInherited(QStringView name)
{
    return name == u"pPrChange" || name == u"rPrChange" || name == u"tblPrChange" || name == u"trPrChange"
        || name == u"tcPrChange" || name == u"sectPr" || name == u"cnfStyle";
}

XmlElementPtr childPtr(const XmlElement *parent, QStringView name)
{
    if (parent == nullptr) {
        return nullptr;
    }
    for (const Ooxml::XmlChild &c : parent->children) {
        if (c.element && c.element->ns == kW && c.element->name == name) {
            return c.element;
        }
    }
    return nullptr;
}

double tabPosition(const XmlElement *tab)
{
    return Ooxml::Units::parseTwips(wAttr(tab, u"pos"));
}

void mergeTabs(XmlElement &result, const XmlElement &overTabs)
{
    XmlElement *tabs = result.child(kW, u"tabs");
    if (tabs == nullptr) {
        tabs = &result.appendElement(kW, QStringLiteral("tabs"));
    }
    for (const XmlElement *tab : overTabs.elements(kW, u"tab")) {
        const double pos = tabPosition(tab);
        tabs->children.removeIf([pos](const Ooxml::XmlChild &c) {
            return c.element && qAbs(tabPosition(c.element.get()) - pos) < 0.5;
        });
        if (wAttr(tab, u"val") != u"clear") {
            tabs->appendElement(tab->clone());
        }
    }
    std::stable_sort(tabs->children.begin(), tabs->children.end(),
                     [](const Ooxml::XmlChild &a, const Ooxml::XmlChild &b) {
                         if (!a.element || !b.element) {
                             return false;
                         }
                         return tabPosition(a.element.get()) < tabPosition(b.element.get());
                     });
    if (tabs->children.isEmpty()) {
        result.removeChildren(kW, u"tabs");
    }
}

TableStyleLayer layerOf(const XmlElement *e)
{
    TableStyleLayer layer;
    layer.pPr = childPtr(e, u"pPr");
    layer.rPr = childPtr(e, u"rPr");
    layer.tblPr = childPtr(e, u"tblPr");
    layer.trPr = childPtr(e, u"trPr");
    layer.tcPr = childPtr(e, u"tcPr");
    return layer;
}

TableStyleLayer mergeLayers(const TableStyleLayer &base, const TableStyleLayer &over)
{
    TableStyleLayer out;
    out.pPr = mergeProperties(base.pPr.get(), over.pPr.get());
    out.rPr = mergeProperties(base.rPr.get(), over.rPr.get());
    out.tblPr = mergeProperties(base.tblPr.get(), over.tblPr.get());
    out.trPr = mergeProperties(base.trPr.get(), over.trPr.get());
    out.tcPr = mergeProperties(base.tcPr.get(), over.tcPr.get());
    return out;
}

// Word's behaviour when docDefaults says nothing: 10 pt Times New Roman.
XmlElementPtr implicitRunDefaults()
{
    auto rPr = std::make_shared<XmlElement>(kW, QStringLiteral("rPr"));
    XmlElement &fonts = rPr->appendElement(kW, QStringLiteral("rFonts"));
    fonts.setAttribute(kW, QStringLiteral("ascii"), QStringLiteral("Times New Roman"));
    fonts.setAttribute(kW, QStringLiteral("hAnsi"), QStringLiteral("Times New Roman"));
    rPr->appendElement(kW, QStringLiteral("sz")).setAttribute(kW, QStringLiteral("val"), QStringLiteral("20"));
    return rPr;
}

} // namespace

QString wAttr(const XmlElement *element, QStringView name, const QString &fallback)
{
    if (element == nullptr) {
        return fallback;
    }
    for (const Ooxml::XmlAttribute &a : element->attributes) {
        if (a.name == name && (a.ns == kW || a.ns.isEmpty())) {
            return a.value;
        }
    }
    return fallback;
}

QString childVal(const XmlElement *parent, QStringView name, const QString &fallback)
{
    if (parent == nullptr) {
        return fallback;
    }
    const XmlElement *child = parent->child(kW, name);
    return child == nullptr ? fallback : wAttr(child, u"val", fallback);
}

bool onOff(const XmlElement *parent, QStringView name, bool absent)
{
    if (parent == nullptr) {
        return absent;
    }
    const XmlElement *child = parent->child(kW, name);
    if (child == nullptr) {
        return absent;
    }
    return Ooxml::Units::parseOnOff(wAttr(child, u"val"), true);
}

XmlElementPtr mergeProperties(const XmlElement *base, const XmlElement *over)
{
    if (base == nullptr && over == nullptr) {
        return nullptr;
    }
    if (over == nullptr) {
        return base->clone();
    }
    XmlElementPtr result = base != nullptr ? base->clone() : std::make_shared<XmlElement>(over->ns, over->name);
    result->ns = over->ns;
    result->name = over->name;
    for (const Ooxml::XmlAttribute &a : over->attributes) {
        result->setAttribute(a.ns, a.name, a.value);
    }
    for (const Ooxml::XmlChild &c : over->children) {
        if (!c.element) {
            continue;
        }
        const XmlElement &child = *c.element;
        if (isNotInherited(child.name)) {
            continue;
        }
        if (child.ns == kW && child.name == u"tabs") {
            mergeTabs(*result, child);
            continue;
        }
        bool merged = false;
        for (Ooxml::XmlChild &existing : result->children) {
            if (existing.element && existing.element->is(child.ns, child.name)) {
                existing.element = mergeProperties(existing.element.get(), &child);
                merged = true;
                break;
            }
        }
        if (!merged) {
            result->appendElement(child.clone());
        }
    }
    return result;
}

StyleSheet StyleSheet::parse(const XmlElement *root)
{
    StyleSheet sheet;
    XmlElementPtr implicit = implicitRunDefaults();
    sheet.m_defaultRPr = implicit;
    if (root == nullptr) {
        return sheet;
    }
    if (const XmlElement *defaults = root->child(kW, u"docDefaults")) {
        if (const XmlElement *rDefault = defaults->child(kW, u"rPrDefault")) {
            sheet.m_defaultRPr = mergeProperties(implicit.get(), rDefault->child(kW, u"rPr"));
        }
        if (const XmlElement *pDefault = defaults->child(kW, u"pPrDefault")) {
            sheet.m_defaultPPr = childPtr(pDefault, u"pPr");
        }
    }
    for (const XmlElement *e : root->elements(kW, u"style")) {
        Style style;
        style.id = wAttr(e, u"styleId");
        style.type = wAttr(e, u"type", QStringLiteral("paragraph"));
        style.isDefault = Ooxml::Units::parseOnOff(wAttr(e, u"default"), false);
        style.name = childVal(e, u"name");
        style.basedOn = childVal(e, u"basedOn");
        style.next = childVal(e, u"next");
        style.pPr = childPtr(e, u"pPr");
        style.rPr = childPtr(e, u"rPr");
        style.table.tblPr = childPtr(e, u"tblPr");
        style.table.trPr = childPtr(e, u"trPr");
        style.table.tcPr = childPtr(e, u"tcPr");
        style.table.pPr = style.pPr;
        style.table.rPr = style.rPr;
        for (const XmlElement *cond : e->elements(kW, u"tblStylePr")) {
            style.conditions.insert(wAttr(cond, u"type"), layerOf(cond));
        }
        if (style.id.isEmpty()) {
            continue;
        }
        if (style.isDefault) {
            if (style.type == u"paragraph" && sheet.m_defaultParagraph.isEmpty()) {
                sheet.m_defaultParagraph = style.id;
            } else if (style.type == u"character" && sheet.m_defaultCharacter.isEmpty()) {
                sheet.m_defaultCharacter = style.id;
            } else if (style.type == u"table" && sheet.m_defaultTable.isEmpty()) {
                sheet.m_defaultTable = style.id;
            }
        }
        if (!style.name.isEmpty()) {
            sheet.m_idByName.insert(style.name.toLower(), style.id);
        }
        sheet.m_styles.insert(style.id, std::move(style));
    }
    return sheet;
}

const Style *StyleSheet::style(const QString &id) const
{
    const auto it = m_styles.constFind(id);
    return it == m_styles.constEnd() ? nullptr : &it.value();
}

QString StyleSheet::idForName(const QString &name) const
{
    return m_idByName.value(name.toLower());
}

QList<const Style *> StyleSheet::chain(const QString &id) const
{
    QList<const Style *> out;
    QSet<QString> seen;
    QString current = id;
    while (!current.isEmpty() && !seen.contains(current) && out.size() < 32) {
        seen.insert(current);
        const Style *s = style(current);
        if (s == nullptr) {
            break;
        }
        out.prepend(s);
        current = s->basedOn;
    }
    return out;
}

QStringList StyleSheet::chainOf(const QString &id) const
{
    QStringList out;
    for (const Style *s : chain(id)) {
        out.append(s->id);
    }
    return out;
}

XmlElementPtr StyleSheet::paragraphProperties(const QString &styleId) const
{
    const QString id = style(styleId) != nullptr ? styleId : m_defaultParagraph;
    const auto cached = m_pPrCache.constFind(id);
    if (cached != m_pPrCache.constEnd()) {
        return cached.value();
    }
    XmlElementPtr result = m_defaultPPr ? m_defaultPPr->clone() : nullptr;
    for (const Style *s : chain(id)) {
        result = mergeProperties(result.get(), s->pPr.get());
    }
    if (!result) {
        result = std::make_shared<XmlElement>(kW, QStringLiteral("pPr"));
    }
    m_pPrCache.insert(id, result);
    return result;
}

XmlElementPtr StyleSheet::paragraphRunProperties(const QString &styleId) const
{
    const QString id = style(styleId) != nullptr ? styleId : m_defaultParagraph;
    const auto cached = m_rPrCache.constFind(id);
    if (cached != m_rPrCache.constEnd()) {
        return cached.value();
    }
    XmlElementPtr result = m_defaultRPr->clone();
    for (const Style *s : chain(id)) {
        result = mergeProperties(result.get(), s->rPr.get());
    }
    m_rPrCache.insert(id, result);
    return result;
}

XmlElementPtr StyleSheet::characterRunProperties(const QString &styleId) const
{
    XmlElementPtr result;
    for (const Style *s : chain(styleId.isEmpty() ? m_defaultCharacter : styleId)) {
        result = mergeProperties(result.get(), s->rPr.get());
    }
    return result;
}

const XmlElement *StyleSheet::styleNumbering(const QString &styleId) const
{
    const XmlElementPtr pPr = paragraphProperties(styleId);
    return pPr ? pPr->child(kW, u"numPr") : nullptr;
}

TableStyleLayer StyleSheet::tableLayer(const QString &styleId) const
{
    TableStyleLayer out;
    for (const Style *s : chain(styleId)) {
        out = mergeLayers(out, s->table);
    }
    return out;
}

TableStyleLayer StyleSheet::tableCondition(const QString &styleId, const QString &type) const
{
    TableStyleLayer out;
    for (const Style *s : chain(styleId)) {
        const auto it = s->conditions.constFind(type);
        if (it != s->conditions.constEnd()) {
            out = mergeLayers(out, it.value());
        }
    }
    return out;
}

int StyleSheet::headingLevel(const QString &styleId) const
{
    const QList<const Style *> styles = chain(styleId);
    for (auto it = styles.crbegin(); it != styles.crend(); ++it) {
        const QString name = (*it)->name.toLower();
        if (name.startsWith(u"heading ")) {
            bool ok = false;
            const int level = name.mid(8).toInt(&ok);
            if (ok && level >= 1 && level <= 9) {
                return level;
            }
        }
        const QString outline = childVal((*it)->pPr.get(), u"outlineLvl");
        if (!outline.isEmpty()) {
            const int level = outline.toInt() + 1;
            return level >= 1 && level <= 9 ? level : 0;
        }
    }
    return 0;
}

bool StyleSheet::tableStyleBeatsParagraphStyle(const QString &paragraphStyleId) const
{
    return paragraphStyleId.isEmpty() || paragraphStyleId == m_defaultParagraph
        || style(paragraphStyleId) == nullptr;
}

} // namespace QindaOffice::Docx
