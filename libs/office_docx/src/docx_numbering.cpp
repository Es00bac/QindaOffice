// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_numbering.h"

#include "docx_styles.h"
#include "ooxml_namespaces.h"

namespace QindaOffice::Docx {

using Ooxml::XmlElement;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

Ooxml::XmlElementPtr childPtr(const XmlElement *parent, QStringView name)
{
    for (const Ooxml::XmlChild &c : parent->children) {
        if (c.element && c.element->ns == kW && c.element->name == name) {
            return c.element;
        }
    }
    return nullptr;
}

} // namespace

NumberingLevel parseNumberingLevel(const XmlElement *lvl)
{
    NumberingLevel level;
    if (lvl == nullptr) {
        return level;
    }
    bool ok = false;
    const int start = childVal(lvl, u"start").toInt(&ok);
    if (ok) {
        level.start = start;
    }
    level.format = childVal(lvl, u"numFmt", QStringLiteral("decimal"));
    level.text = childVal(lvl, u"lvlText");
    level.justification = childVal(lvl, u"lvlJc", QStringLiteral("left"));
    level.suffix = childVal(lvl, u"suff", QStringLiteral("tab"));
    const QString restart = childVal(lvl, u"lvlRestart");
    if (!restart.isEmpty()) {
        // w:lvlRestart val = N restarts after level N (1-based); 0 never.
        level.restartAfterLevel = restart.toInt() - 1;
    }
    level.legal = onOff(lvl, u"isLgl");
    level.pPr = childPtr(lvl, u"pPr");
    level.rPr = childPtr(lvl, u"rPr");
    return level;
}

Numbering Numbering::parse(const XmlElement *root)
{
    Numbering numbering;
    if (root == nullptr) {
        return numbering;
    }
    for (const Ooxml::XmlChild &child : root->children) {
        const XmlElement *a = child.element.get();
        if (a == nullptr || !a->is(kW, u"abstractNum")) {
            continue;
        }
        Abstract abstract;
        abstract.xml = child.element;
        for (const XmlElement *lvl : a->elements(kW, u"lvl")) {
            abstract.levels.insert(wAttr(lvl, u"ilvl").toInt(), parseNumberingLevel(lvl));
        }
        abstract.numStyleLink = childVal(a, u"numStyleLink");
        const QString styleLink = childVal(a, u"styleLink");
        const QString id = wAttr(a, u"abstractNumId");
        if (!styleLink.isEmpty()) {
            numbering.m_styleLinks.insert(styleLink, id);
        }
        numbering.m_abstracts.insert(id, std::move(abstract));
    }
    for (const XmlElement *num : root->elements(kW, u"num")) {
        Instance instance;
        instance.abstractId = childVal(num, u"abstractNumId");
        for (const XmlElement *over : num->elements(kW, u"lvlOverride")) {
            const int ilvl = wAttr(over, u"ilvl").toInt();
            const QString start = childVal(over, u"startOverride");
            if (!start.isEmpty()) {
                instance.startOverrides.insert(ilvl, start.toInt());
            }
            if (const XmlElement *lvl = over->child(kW, u"lvl")) {
                instance.levelOverrides.insert(ilvl, parseNumberingLevel(lvl));
                for (const Ooxml::XmlChild &c : over->children) {
                    if (c.element && c.element->is(kW, u"lvl")) {
                        instance.levelOverrideXml.insert(ilvl, c.element);
                    }
                }
            }
        }
        numbering.m_instances.insert(wAttr(num, u"numId").toInt(), std::move(instance));
    }
    return numbering;
}

QString Numbering::abstractIdOf(int numId) const
{
    const auto it = m_instances.constFind(numId);
    if (it == m_instances.constEnd()) {
        return {};
    }
    QString id = it->abstractId;
    // An abstract that defers to a numbering style: follow the link once.
    const auto abstract = m_abstracts.constFind(id);
    if (abstract != m_abstracts.constEnd() && abstract->levels.isEmpty() && !abstract->numStyleLink.isEmpty()) {
        id = m_styleLinks.value(abstract->numStyleLink, id);
    }
    return id;
}

const NumberingLevel *Numbering::level(int numId, int ilvl) const
{
    if (numId <= 0) {
        return nullptr;
    }
    const auto instance = m_instances.constFind(numId);
    if (instance == m_instances.constEnd()) {
        return nullptr;
    }
    const auto over = instance->levelOverrides.constFind(ilvl);
    if (over != instance->levelOverrides.constEnd()) {
        return &over.value();
    }
    const auto abstract = m_abstracts.constFind(abstractIdOf(numId));
    if (abstract == m_abstracts.constEnd()) {
        return nullptr;
    }
    const auto lvl = abstract->levels.constFind(ilvl);
    return lvl == abstract->levels.constEnd() ? nullptr : &lvl.value();
}

int Numbering::start(int numId, int ilvl) const
{
    const auto instance = m_instances.constFind(numId);
    if (instance != m_instances.constEnd()) {
        const auto over = instance->startOverrides.constFind(ilvl);
        if (over != instance->startOverrides.constEnd()) {
            return over.value();
        }
    }
    const NumberingLevel *lvl = level(numId, ilvl);
    return lvl != nullptr ? lvl->start : 1;
}

QList<int> Numbering::startOverrides(int numId) const
{
    QList<int> out(9, -1);
    const auto instance = m_instances.constFind(numId);
    if (instance != m_instances.constEnd()) {
        for (auto it = instance->startOverrides.cbegin(); it != instance->startOverrides.cend(); ++it) {
            if (it.key() >= 0 && it.key() < 9) {
                out[it.key()] = it.value();
            }
        }
    }
    return out;
}

QString Numbering::definitionXml(int numId) const
{
    if (const auto cached = m_definitions.constFind(numId); cached != m_definitions.constEnd()) {
        return cached.value();
    }
    const auto instance = m_instances.constFind(numId);
    const auto abstract = m_abstracts.constFind(abstractIdOf(numId));
    if (instance == m_instances.constEnd() || abstract == m_abstracts.constEnd() || !abstract->xml) {
        return {};
    }
    QString xml;
    if (instance->levelOverrideXml.isEmpty()) {
        xml = Ooxml::serializeFragment(*abstract->xml);
    } else {
        // An instance that redefines levels: those levels replace the
        // abstract's (a new definition when the list is written changed).
        const Ooxml::XmlElementPtr copy = abstract->xml->clone();
        for (Ooxml::XmlChild &c : copy->children) {
            if (c.element && c.element->is(kW, u"lvl")) {
                const int ilvl = wAttr(c.element.get(), u"ilvl").toInt();
                if (const auto over = instance->levelOverrideXml.constFind(ilvl); over != instance->levelOverrideXml.constEnd()) {
                    c.element = over.value()->clone();
                    c.element->setAttribute(kW, QStringLiteral("ilvl"), QString::number(ilvl));
                }
            }
        }
        xml = Ooxml::serializeFragment(*copy);
    }
    m_definitions.insert(numId, xml);
    return xml;
}

} // namespace QindaOffice::Docx
