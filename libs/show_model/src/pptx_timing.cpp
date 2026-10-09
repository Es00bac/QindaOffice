// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_timing.h"

#include "pptx_timing_builder.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string_view>

namespace QindaOffice::Show::Pptx {

namespace {

const Ooxml::XmlElement *firstEffect(const Ooxml::XmlElement *transition)
{
    if (transition == nullptr) {
        return nullptr;
    }
    for (const Ooxml::XmlElement *e : transition->elements()) {
        if (e->name != u"sndAc" && e->name != u"extLst") {
            return e;
        }
    }
    return nullptr;
}

ShapeAnimation animationFor(int presetId, int subtype)
{
    switch (presetId) {
    case 1:
        return ShapeAnimation::Appear;
    case 2:
        switch (subtype) {
        case 1:
            return ShapeAnimation::FlyInTop;
        case 2:
            return ShapeAnimation::FlyInRight;
        case 8:
            return ShapeAnimation::FlyInLeft;
        default:
            return ShapeAnimation::FlyInBottom;
        }
    case 23:
    case 53:
        return ShapeAnimation::Zoom;
    case 22: // M67
        return ShapeAnimation::Wipe;
    case 16:
        return ShapeAnimation::Split;
    default:
        return ShapeAnimation::Fade;
    }
}

// The emphasis effects the model holds (M67): Spin and Grow/Shrink.
std::optional<ShapeAnimation> emphasisFor(int presetId)
{
    switch (presetId) {
    case 8:
        return ShapeAnimation::Spin;
    case 6:
        return ShapeAnimation::GrowShrink;
    default:
        return std::nullopt;
    }
}

const Ooxml::XmlElement *mainSequence(const Ooxml::XmlElement *timing)
{
    if (timing == nullptr) {
        return nullptr;
    }
    for (const Ooxml::XmlElement *seq = timing->descendant(nsP(), u"seq"); seq != nullptr;) {
        const Ooxml::XmlElement *cTn = seq->child(nsP(), u"cTn");
        if (cTn != nullptr && cTn->attr(u"nodeType") == u"mainSeq") {
            return seq;
        }
        break; // the first sequence of the root is the main one in every deck PowerPoint writes
    }
    return nullptr;
}

int longestBehaviour(const Ooxml::XmlElement &effect)
{
    int longest = 0;
    for (const Ooxml::XmlElement *child : effect.elements()) {
        if (child->is(nsP(), u"cBhvr")) {
            if (const Ooxml::XmlElement *timed = child->child(nsP(), u"cTn")) {
                longest = qMax(longest, timed->attr(u"dur").toInt());
            }
        }
        longest = qMax(longest, longestBehaviour(*child));
    }
    return longest;
}

void collectEntrances(const Ooxml::XmlElement &node, int step, QList<BuildEntry> &out)
{
    for (const Ooxml::XmlElement *child : node.elements()) {
        const bool entrance = child->is(nsP(), u"cTn") && child->attr(u"presetClass") == u"entr";
        const std::optional<ShapeAnimation> emphasis = child->is(nsP(), u"cTn") && child->attr(u"presetClass") == u"emph"
            ? emphasisFor(child->attr(u"presetID").toInt())
            : std::nullopt;
        if (entrance || emphasis) {
            const Ooxml::XmlElement *target = child->descendant(nsP(), u"spTgt");
            if (target != nullptr) {
                BuildEntry entry;
                entry.shapeId = target->attr(u"spid").toUInt();
                entry.step = step;
                entry.animation = emphasis ? *emphasis
                                           : animationFor(child->attr(u"presetID").toInt(), child->attr(u"presetSubtype").toInt());
                // The longest behaviour is how long the effect plays (the
                // visibility switch lasts 1 ms).
                entry.durationMs = longestBehaviour(*child);
                const bool seen = std::any_of(out.cbegin(), out.cend(),
                                              [&entry](const BuildEntry &e) { return e.shapeId == entry.shapeId; });
                if (!seen) {
                    out.append(entry);
                }
            }
            continue;
        }
        collectEntrances(*child, step, out);
    }
}

bool mentionsMissing(const Ooxml::XmlElement &node, const QSet<quint32> &present)
{
    if (node.is(nsP(), u"spTgt") || node.is(nsP(), u"bldP") || node.is(nsP(), u"bldGraphic")
        || node.is(nsP(), u"bldOleChart") || node.is(nsP(), u"bldDgm")) {
        const QString spid = node.attr(u"spid");
        if (!spid.isEmpty() && !present.contains(spid.toUInt())) {
            return true;
        }
    }
    for (const Ooxml::XmlElement *child : node.elements()) {
        if (mentionsMissing(*child, present)) {
            return true;
        }
    }
    return false;
}

// Removes every p:par (and build entry) that names a missing shape;
// returns false when `node` itself should go.
bool prune(Ooxml::XmlElement &node, const QSet<quint32> &present)
{
    for (qsizetype i = node.children.size() - 1; i >= 0; --i) {
        Ooxml::XmlChild &child = node.children[i];
        if (!child.element) {
            continue;
        }
        Ooxml::XmlElement &e = *child.element;
        // M75: a media node (p:video / p:audio) names one shape, too.
        const bool doomed = (e.is(nsP(), u"par") || e.name.startsWith(QLatin1String("bld")) || e.is(nsP(), u"video")
                             || e.is(nsP(), u"audio"))
            && mentionsMissing(e, present) && !e.descendant(nsP(), u"par");
        if (doomed) {
            node.children.removeAt(i);
            continue;
        }
        if (!prune(e, present)) {
            node.children.removeAt(i);
        }
    }
    // A time container whose children are all gone has nothing to play.
    if (node.is(nsP(), u"par") || node.is(nsP(), u"seq")) {
        const Ooxml::XmlElement *cTn = node.child(nsP(), u"cTn");
        const Ooxml::XmlElement *children = cTn != nullptr ? cTn->child(nsP(), u"childTnLst") : nullptr;
        if (children != nullptr && children->elements().isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace

QString transitionName(const Ooxml::XmlElement *transition)
{
    if (transition != nullptr && transition->is(nsMc(), u"AlternateContent")) {
        const Ooxml::XmlElement *fallback = transition->child(nsMc(), u"Fallback");
        transition = fallback != nullptr ? fallback->child(nsP(), u"transition") : nullptr;
    }
    const Ooxml::XmlElement *effect = firstEffect(transition);
    if (effect == nullptr) {
        return QStringLiteral("cut");
    }
    const QString &name = effect->name;
    if (name == u"cut") {
        return QStringLiteral("cut");
    }
    if (name == u"wipe" || name == u"split") { // M67: played as themselves
        return name;
    }
    if (name == u"push" || name == u"cover" || name == u"pull" || name == u"strips" || name == u"randomBar"
        || name == u"blinds" || name == u"comb" || name == u"checker") {
        return QStringLiteral("slide");
    }
    if (name == u"zoom" || name == u"newsflash" || name == u"circle" || name == u"diamond" || name == u"plus") {
        return QStringLiteral("zoom");
    }
    return QStringLiteral("fade");
}

Ooxml::XmlElementPtr transitionElement(const QString &name)
{
    if (name.isEmpty() || name == u"cut") {
        return nullptr;
    }
    auto transition = std::make_shared<Ooxml::XmlElement>(nsP(), QStringLiteral("transition"));
    transition->setAttribute(QString(), QStringLiteral("spd"), QStringLiteral("med"));
    if (name == u"slide") {
        transition->appendElement(nsP(), QStringLiteral("push")).setAttribute(QString(), QStringLiteral("dir"), QStringLiteral("l"));
    } else if (name == u"wipe") { // M67: PowerPoint's Wipe, from the right
        transition->appendElement(nsP(), QStringLiteral("wipe")).setAttribute(QString(), QStringLiteral("dir"), QStringLiteral("l"));
    } else if (name == u"split") { // Split, vertical out
        Ooxml::XmlElement &split = transition->appendElement(nsP(), QStringLiteral("split"));
        split.setAttribute(QString(), QStringLiteral("orient"), QStringLiteral("vert"));
        split.setAttribute(QString(), QStringLiteral("dir"), QStringLiteral("out"));
    } else if (name == u"zoom") {
        transition->appendElement(nsP(), QStringLiteral("zoom"));
    } else {
        transition->appendElement(nsP(), QStringLiteral("fade"));
    }
    return transition;
}

QList<BuildEntry> readBuilds(const Ooxml::XmlElement *timing)
{
    QList<BuildEntry> builds;
    const Ooxml::XmlElement *seq = mainSequence(timing);
    const Ooxml::XmlElement *cTn = seq != nullptr ? seq->child(nsP(), u"cTn") : nullptr;
    const Ooxml::XmlElement *clicks = cTn != nullptr ? cTn->child(nsP(), u"childTnLst") : nullptr;
    if (clicks == nullptr) {
        return builds;
    }
    const QList<const Ooxml::XmlElement *> pars = clicks->elements(nsP(), u"par");
    const QList<int> steps = clickSteps(*clicks);
    for (qsizetype i = 0; i < pars.size(); ++i) {
        if (steps.at(i) > 0) {
            collectEntrances(*pars.at(i), steps.at(i), builds);
        }
    }
    return builds;
}

bool startsWithSlide(const Ooxml::XmlElement &par)
{
    const Ooxml::XmlElement *cTn = par.child(nsP(), u"cTn");
    const Ooxml::XmlElement *conditions = cTn != nullptr ? cTn->child(nsP(), u"stCondLst") : nullptr;
    if (conditions == nullptr) {
        return false;
    }
    for (const Ooxml::XmlElement *cond : conditions->elements(nsP(), u"cond")) {
        if (cond->attr(u"evt") == u"onBegin") {
            return true;
        }
    }
    return false;
}

QList<int> clickSteps(const Ooxml::XmlElement &clicks)
{
    const std::function<bool(const Ooxml::XmlElement &)> builds = [&builds](const Ooxml::XmlElement &e) {
        if (e.is(nsP(), u"cTn") && (e.attr(u"presetClass") == u"entr" || e.attr(u"presetClass") == u"emph")) {
            return true;
        }
        const QList<const Ooxml::XmlElement *> children = e.elements();
        return std::any_of(children.cbegin(), children.cend(), [&builds](const Ooxml::XmlElement *c) { return builds(*c); });
    };
    QList<int> steps;
    int step = 0;
    for (const Ooxml::XmlElement *par : clicks.elements(nsP(), u"par")) {
        steps.append(startsWithSlide(*par) && !builds(*par) ? 0 : ++step);
    }
    return steps;
}


Ooxml::XmlElementPtr timingElement(const QList<BuildEntry> &builds, const QList<MediaEntry> &media)
{
    // M75: a media shape plays with the slide (autoPlay), at its click, or
    // only when clicked on (no command — its media node alone).
    QList<MediaEntry> automatic;
    QList<MediaEntry> clicked;
    for (const MediaEntry &entry : media) {
        if (entry.autoPlay) {
            automatic.append(entry);
        } else if (entry.step > 0) {
            clicked.append(entry);
        }
    }
    if (builds.isEmpty() && media.isEmpty()) {
        return nullptr;
    }
    QList<BuildEntry> ordered = builds;
    std::stable_sort(ordered.begin(), ordered.end(), [](const BuildEntry &a, const BuildEntry &b) { return a.step < b.step; });
    QList<int> steps;
    for (const BuildEntry &entry : std::as_const(ordered)) {
        steps.append(entry.step);
    }
    for (const MediaEntry &entry : std::as_const(clicked)) {
        steps.append(entry.step);
    }
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
    TimingBuilder b;
    auto timing = std::make_shared<Ooxml::XmlElement>(nsP(), QStringLiteral("timing"));
    Ooxml::XmlElement &rootPar = timing->appendElement(nsP(), QStringLiteral("tnLst")).appendElement(nsP(), QStringLiteral("par"));
    Ooxml::XmlElement &root = b.cTn(rootPar);
    root.setAttribute(QString(), QStringLiteral("dur"), QStringLiteral("indefinite"));
    root.setAttribute(QString(), QStringLiteral("restart"), QStringLiteral("never"));
    root.setAttribute(QString(), QStringLiteral("nodeType"), QStringLiteral("tmRoot"));
    Ooxml::XmlElement &rootChildren = root.appendElement(nsP(), QStringLiteral("childTnLst"));
    if (!steps.isEmpty() || !automatic.isEmpty()) {
        Ooxml::XmlElement &seq = rootChildren.appendElement(nsP(), QStringLiteral("seq"));
        seq.setAttribute(QString(), QStringLiteral("concurrent"), QStringLiteral("1"));
        seq.setAttribute(QString(), QStringLiteral("nextAc"), QStringLiteral("seek"));
        Ooxml::XmlElement &main = b.cTn(seq);
        main.setAttribute(QString(), QStringLiteral("dur"), QStringLiteral("indefinite"));
        main.setAttribute(QString(), QStringLiteral("nodeType"), QStringLiteral("mainSeq"));
        Ooxml::XmlElement &clicks = main.appendElement(nsP(), QStringLiteral("childTnLst"));
        if (!automatic.isEmpty()) {
            appendAutoPlay(b, clicks, main.attr(u"id"), automatic);
        }
        for (const int step : std::as_const(steps)) {
            Ooxml::XmlElement &click = b.cTn(clicks.appendElement(nsP(), QStringLiteral("par")));
            click.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
            TimingBuilder::delay(click, QStringLiteral("indefinite"));
            Ooxml::XmlElement &group = b.cTn(click.appendElement(nsP(), QStringLiteral("childTnLst")).appendElement(nsP(), QStringLiteral("par")));
            group.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
            TimingBuilder::delay(group, QStringLiteral("0"));
            Ooxml::XmlElement &effects = group.appendElement(nsP(), QStringLiteral("childTnLst"));
            bool first = true;
            for (const BuildEntry &entry : std::as_const(ordered)) {
                if (entry.step == step) {
                    b.effect(effects, entry, first);
                    first = false;
                }
            }
            for (const MediaEntry &entry : std::as_const(clicked)) {
                if (entry.step == step) {
                    appendMediaCall(b, effects, entry, first);
                    first = false;
                }
            }
        }
        for (const char16_t *list : {u"prevCondLst", u"nextCondLst"}) {
            Ooxml::XmlElement &cond = seq.appendElement(nsP(), QString::fromUtf16(list)).appendElement(nsP(), QStringLiteral("cond"));
            cond.setAttribute(QString(), QStringLiteral("evt"), list == std::u16string_view(u"prevCondLst") ? QStringLiteral("onPrev") : QStringLiteral("onNext"));
            cond.setAttribute(QString(), QStringLiteral("delay"), QStringLiteral("0"));
            cond.appendElement(nsP(), QStringLiteral("tgtEl")).appendElement(nsP(), QStringLiteral("sldTgt"));
        }
    }
    appendMediaNodes(b, rootChildren, media);
    return timing;
}

Ooxml::XmlElementPtr pruneTiming(const Ooxml::XmlElement &timing, const QSet<quint32> &present)
{
    if (!mentionsMissing(timing, present)) {
        return timing.clone();
    }
    Ooxml::XmlElementPtr copy = timing.clone();
    prune(*copy, present);
    const Ooxml::XmlElement *list = copy->child(nsP(), u"tnLst");
    if (list == nullptr || list->elements().isEmpty() || copy->descendant(nsP(), u"spTgt") == nullptr) {
        return nullptr;
    }
    // An emptied build list is not allowed (bldLst needs a child).
    if (Ooxml::XmlElement *bld = copy->child(nsP(), u"bldLst"); bld != nullptr && bld->elements().isEmpty()) {
        copy->removeChildren(nsP(), u"bldLst");
    }
    return copy;
}

} // namespace QindaOffice::Show::Pptx
