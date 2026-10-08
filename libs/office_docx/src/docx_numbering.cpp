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

NumberingLevel parseLevel(const XmlElement *lvl)
{
    NumberingLevel level;
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

} // namespace

Numbering Numbering::parse(const XmlElement *root)
{
    Numbering numbering;
    if (root == nullptr) {
        return numbering;
    }
    for (const XmlElement *a : root->elements(kW, u"abstractNum")) {
        Abstract abstract;
        for (const XmlElement *lvl : a->elements(kW, u"lvl")) {
            abstract.levels.insert(wAttr(lvl, u"ilvl").toInt(), parseLevel(lvl));
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
                instance.levelOverrides.insert(ilvl, parseLevel(lvl));
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

} // namespace QindaOffice::Docx
