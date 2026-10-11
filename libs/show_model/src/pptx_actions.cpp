// SPDX-License-Identifier: GPL-3.0-or-later
// Action settings in PresentationML (M91, D-099): a:hlinkClick,
// a:hlinkHover and a:hlinkMouseOver read into ShowAction (pptx_context.h)
// and written from it (pptx_write_context.h).
//
// PowerPoint's spelling, per action:
//   next / previous / first / last / last viewed slide, end show
//       r:id="" action="ppaction://hlinkshowjump?jump=nextslide" (…)
//   a slide        r:id → the slide part, action="ppaction://hlinksldjump"
//   a custom show  r:id="" action="ppaction://customshow?id=0&return=true"
//   a web address  r:id → an external hyperlink relationship, no action
//   another file   r:id → external, action="ppaction://hlinkfile"
//   a program      r:id → external, action="ppaction://program"
//   a macro        r:id="" action="ppaction://macro?name=…"
//   no action      r:id="" action="ppaction://noaction" (a sound or a highlight alone)
//   a sound        <a:snd r:embed → the WAV part (…/audio) name="…"/>
// plus @tooltip, @highlightClick and @endSnd ([Stop Previous Sound]).

#include "pptx_context.h"
#include "pptx_write_context.h"

#include "ooxml_units.h"

namespace QindaOffice::Show::Pptx {

namespace {

const QString kNoAction = QStringLiteral("ppaction://noaction");
const QString kSlideJump = QStringLiteral("ppaction://hlinksldjump");

bool namesExternalTarget(const QString &verb)
{
    for (const char *name : {"ppaction://program", "ppaction://hlinkfile", "ppaction://hlinkpres"}) {
        if (verb == QLatin1String(name) || verb.startsWith(QLatin1String(name) + QLatin1Char('?'))) {
            return true;
        }
    }
    return false;
}

} // namespace

ShowAction actionFrom(const PartContext &ctx, const Ooxml::XmlElement *hlink)
{
    ShowAction action;
    if (hlink == nullptr) {
        return action;
    }
    const QString id = hlink->attribute(nsR(), u"id");
    const QString verb = hlink->attr(u"action");
    QString external;
    QString internal;
    if (!id.isEmpty() && ctx.package) {
        if (const std::optional<Ooxml::Relationship> rel = ctx.package->relationship(ctx.part, id)) {
            if (rel->external) {
                external = rel->target;
            } else {
                internal = ctx.package->targetPart(ctx.part, *rel);
            }
        }
    }
    const bool slideTarget = ctx.slideIds != nullptr && !internal.isEmpty() && ctx.slideIds->contains(internal);
    if (slideTarget && (verb.isEmpty() || verb.startsWith(kSlideJump))) {
        action.link = QLatin1Char('#') + ctx.slideIds->value(internal);
    } else if (verb.startsWith(kSlideJump)) {
        action.link.clear(); // a jump to a slide this package does not have
    } else if (verb == kNoAction) {
        action.link.clear(); // the same as no link (written back as noaction)
    } else if (!verb.isEmpty()) {
        action.link = verb;
        if (namesExternalTarget(verb)) {
            action.address = external;
        }
    } else {
        action.link = external;
    }
    action.tooltip = hlink->attr(u"tooltip");
    action.highlight = Ooxml::Units::parseOnOff(hlink->attr(u"highlightClick"), false);
    action.stopSound = Ooxml::Units::parseOnOff(hlink->attr(u"endSnd"), false);
    if (const Ooxml::XmlElement *sound = hlink->child(nsA(), u"snd")) {
        const RelatedData data = relatedData(ctx, sound->attribute(nsR(), u"embed"));
        action.sound = data.bytes;
        action.soundName = sound->attr(u"name");
    }
    return action;
}

Ooxml::XmlElementPtr PartWriter::action(const ShowAction &model, const QString &element)
{
    if (model.isEmpty()) {
        return nullptr;
    }
    QString relationship;
    QString verb;
    bool hasTarget = true;
    if (model.link.startsWith(QLatin1Char('#'))) {
        const QString slidePart = session.slideParts.value(model.link.mid(1));
        if (slidePart.isEmpty()) {
            hasTarget = false; // the slide is gone
        } else {
            relationship = relate(relType("slide"), slidePart);
            verb = kSlideJump;
        }
    } else if (model.link.startsWith(QLatin1String("ppaction:"))) {
        verb = model.link;
        if (namesExternalTarget(verb) && !model.address.isEmpty()) {
            relationship = relate(relType("hyperlink"), model.address, true);
        }
    } else if (!model.link.isEmpty()) {
        relationship = relate(relType("hyperlink"), model.link, true);
    } else {
        hasTarget = false;
    }
    const bool extras = !model.sound.isEmpty() || model.stopSound || model.highlight || !model.tooltip.isEmpty();
    if (!hasTarget && !extras) {
        return nullptr;
    }
    if (!hasTarget) {
        verb = kNoAction;
    }
    auto link = std::make_shared<Ooxml::XmlElement>(nsA(), element);
    link->setAttribute(nsR(), QStringLiteral("id"), relationship);
    if (!verb.isEmpty()) {
        link->setAttribute(QString(), QStringLiteral("action"), verb);
    }
    if (!model.tooltip.isEmpty()) {
        link->setAttribute(QString(), QStringLiteral("tooltip"), model.tooltip);
    }
    if (model.highlight) {
        link->setAttribute(QString(), QStringLiteral("highlightClick"), QStringLiteral("1"));
    }
    if (model.stopSound) {
        link->setAttribute(QString(), QStringLiteral("endSnd"), QStringLiteral("1"));
    }
    if (!model.sound.isEmpty()) {
        // a:snd holds WAV only (CT_EmbeddedWAVAudioFile).
        const QString name = model.soundName.isEmpty() ? QStringLiteral("sound.wav") : model.soundName;
        const QString part = session.mediaFile(model.sound, QStringLiteral("audio/wav"), name);
        Ooxml::XmlElement &sound = link->appendElement(nsA(), QStringLiteral("snd"));
        sound.setAttribute(nsR(), QStringLiteral("embed"), relate(relType("audio"), part));
        sound.setAttribute(QString(), QStringLiteral("name"), model.soundName);
    }
    return link;
}

} // namespace QindaOffice::Show::Pptx
