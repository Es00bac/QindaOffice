// SPDX-License-Identifier: GPL-3.0-or-later
// Custom shows and Set Up Slide Show in PresentationML (pptx_show.h).

#include "pptx_show.h"

#include "pptx_review.h"
#include "pptx_write_text.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const char *name, const QString &value)
{
    e.setAttribute(QString(), QLatin1String(name), value);
}

bool flag(const XmlElement &e, QStringView name, bool absent)
{
    return Ooxml::Units::parseOnOff(e.attr(name), absent);
}

// A colour the way presProps spells it (srgbClr, prstClr, sysClr…), read
// against the deck's first master (a scheme colour there is unusual).
QColor colorIn(const DeckReadState &state, const QString &part, const XmlElement *container, const QColor &fallback)
{
    if (container == nullptr || state.masters.empty()) {
        return fallback;
    }
    const MasterInfo &master = state.masters.front();
    const QColor color = childColor(state.context(part, master, master.map), container);
    return color.isValid() ? eightBit(color) : fallback;
}

} // namespace

QList<CustomShow> readCustomShows(const DeckReadState &state)
{
    QList<CustomShow> shows;
    const XmlElement *list = state.presentation ? state.presentation->child(nsP(), u"custShowLst") : nullptr;
    if (list == nullptr) {
        return shows;
    }
    for (const XmlElement *element : list->elements(nsP(), u"custShow")) {
        CustomShow show;
        show.number = element->attr(u"id").toUInt();
        show.name = element->attr(u"name");
        if (const XmlElement *slides = element->child(nsP(), u"sldLst")) {
            for (const XmlElement *slide : slides->elements(nsP(), u"sld")) {
                const auto rel = state.package->relationship(state.main, slide->attribute(nsR(), u"id"));
                const QString part = rel ? state.package->targetPart(state.main, *rel) : QString();
                if (state.slideIds.contains(part)) {
                    show.slides.append(state.slideIds.value(part));
                }
            }
        }
        shows.append(show);
    }
    return shows;
}

ShowSettings readShowSettings(const DeckReadState &state, QString *part)
{
    ShowSettings settings = ShowSettings::fileDefaults();
    const auto rel = state.package->firstOfType(state.main, QString::fromUtf16(Ooxml::RelType::PresProps));
    const QString name = rel ? state.package->targetPart(state.main, *rel) : QString();
    if (part != nullptr) {
        *part = name;
    }
    const XmlElementPtr root = name.isEmpty() ? nullptr : state.package->xml(name);
    const XmlElement *show = root ? root->child(nsP(), u"showPr") : nullptr;
    if (show == nullptr) {
        return settings;
    }
    settings.loop = flag(*show, u"loop", false);
    settings.narration = flag(*show, u"showNarration", false);
    settings.animation = flag(*show, u"showAnimation", true);
    settings.useTimings = flag(*show, u"useTimings", true);
    if (const XmlElement *browse = show->child(nsP(), u"browse")) {
        settings.type = ShowType::Window;
        settings.showScrollbar = flag(*browse, u"showScrollbar", true);
    } else if (const XmlElement *kiosk = show->child(nsP(), u"kiosk")) {
        settings.type = ShowType::Kiosk;
        bool ok = false;
        const int restart = kiosk->attr(u"restart").toInt(&ok);
        settings.kioskRestartMs = ok && restart >= 0 ? restart : 300000;
    }
    if (const XmlElement *range = show->child(nsP(), u"sldRg")) {
        settings.range = ShowRange::Slides;
        settings.from = qMax(1, range->attr(u"st").toInt());
        settings.to = qMax(settings.from, range->attr(u"end").toInt());
    } else if (const XmlElement *custom = show->child(nsP(), u"custShow")) {
        settings.range = ShowRange::Custom;
        settings.customShow = custom->attr(u"id").toUInt();
    }
    settings.penColor = colorIn(state, name, show->child(nsP(), u"penClr"), settings.penColor);
    if (const XmlElement *extensions = show->child(nsP(), u"extLst")) {
        for (const XmlElement *ext : extensions->elements(nsP(), u"ext")) {
            if (ext->attr(u"uri") == QStringView(kLaserColorUri)) {
                settings.laserColor = colorIn(state, name, ext->child(nsP14(), u"laserClr"), settings.laserColor);
            }
        }
    }
    return settings.normalized();
}

XmlElementPtr customShowsElement(const QList<CustomShow> &shows, const QHash<QString, QString> &slideRelationships)
{
    if (shows.isEmpty()) {
        return nullptr;
    }
    XmlElementPtr list = make(nsP(), QStringLiteral("custShowLst"));
    for (const CustomShow &show : shows) {
        XmlElement &e = list->appendElement(nsP(), QStringLiteral("custShow"));
        set(e, "name", show.name);
        set(e, "id", QString::number(show.number));
        XmlElement &slides = e.appendElement(nsP(), QStringLiteral("sldLst"));
        for (const QString &slide : show.slides) {
            const QString rel = slideRelationships.value(slide);
            if (!rel.isEmpty()) {
                slides.appendElement(nsP(), QStringLiteral("sld")).setAttribute(nsR(), QStringLiteral("id"), rel);
            }
        }
    }
    return list;
}

XmlElementPtr showPropertiesElement(const ShowSettings &s, const XmlElement *was)
{
    XmlElementPtr show = make(nsP(), QStringLiteral("showPr"));
    if (s.loop || s.type == ShowType::Kiosk) {
        set(*show, "loop", QStringLiteral("1"));
    }
    if (s.narration) {
        set(*show, "showNarration", QStringLiteral("1"));
    }
    if (!s.animation) {
        set(*show, "showAnimation", QStringLiteral("0"));
    }
    if (!s.useTimings) {
        set(*show, "useTimings", QStringLiteral("0"));
    }
    switch (s.type) {
    case ShowType::Speaker:
        show->appendElement(nsP(), QStringLiteral("present"));
        break;
    case ShowType::Window: {
        XmlElement &browse = show->appendElement(nsP(), QStringLiteral("browse"));
        if (!s.showScrollbar) {
            set(browse, "showScrollbar", QStringLiteral("0"));
        }
        break;
    }
    case ShowType::Kiosk: {
        XmlElement &kiosk = show->appendElement(nsP(), QStringLiteral("kiosk"));
        if (s.kioskRestartMs != 300000) {
            set(kiosk, "restart", QString::number(qMax(0, s.kioskRestartMs)));
        }
        break;
    }
    }
    switch (s.range) {
    case ShowRange::All:
        show->appendElement(nsP(), QStringLiteral("sldAll"));
        break;
    case ShowRange::Slides: {
        XmlElement &range = show->appendElement(nsP(), QStringLiteral("sldRg"));
        set(range, "st", QString::number(qMax(1, s.from)));
        set(range, "end", QString::number(qMax(qMax(1, s.from), s.to)));
        break;
    }
    case ShowRange::Custom:
        set(show->appendElement(nsP(), QStringLiteral("custShow")), "id", QString::number(s.customShow));
        break;
    }
    show->appendElement(nsP(), QStringLiteral("penClr")).appendElement(colorElement(s.penColor));
    XmlElement &extensions = show->appendElement(nsP(), QStringLiteral("extLst"));
    XmlElement &laser = extensions.appendElement(nsP(), QStringLiteral("ext"));
    set(laser, "uri", QString::fromUtf16(kLaserColorUri));
    laser.appendElement(nsP14(), QStringLiteral("laserClr")).appendElement(colorElement(s.laserColor));
    if (const XmlElement *kept = was != nullptr ? was->child(nsP(), u"extLst") : nullptr) {
        for (const Ooxml::XmlChild &child : kept->children) {
            if (child.element && child.element->is(nsP(), u"ext") && child.element->attr(u"uri") != QStringView(kLaserColorUri)) {
                extensions.appendElement(child.element->clone());
            }
        }
    }
    return show;
}

XmlElementPtr presentationPropertiesElement(const ShowSettings &settings, const XmlElement *source)
{
    XmlElementPtr root = source != nullptr ? source->clone() : make(nsP(), QStringLiteral("presentationPr"));
    const XmlElement *was = source != nullptr ? source->child(nsP(), u"showPr") : nullptr;
    root->removeChildren(nsP(), u"showPr");
    // CT_PresentationProperties: htmlPubPr, webPr, prnPr, showPr, clrMru, extLst.
    root->insertElementBefore(showPropertiesElement(settings, was), {u"clrMru", u"extLst"});
    return root;
}

} // namespace QindaOffice::Show::Pptx
