// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_timing.h"

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
    default:
        return ShapeAnimation::Fade;
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
        if (child->is(nsP(), u"cTn") && child->attr(u"presetClass") == u"entr") {
            const Ooxml::XmlElement *target = child->descendant(nsP(), u"spTgt");
            if (target != nullptr) {
                BuildEntry entry;
                entry.shapeId = target->attr(u"spid").toUInt();
                entry.step = step;
                entry.animation = animationFor(child->attr(u"presetID").toInt(), child->attr(u"presetSubtype").toInt());
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
        const bool doomed = (e.is(nsP(), u"par") || e.name.startsWith(QLatin1String("bld")))
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
    if (name == u"push" || name == u"wipe" || name == u"cover" || name == u"pull" || name == u"split"
        || name == u"strips" || name == u"randomBar" || name == u"blinds" || name == u"comb" || name == u"checker") {
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
    int step = 0;
    for (const Ooxml::XmlElement *click : clicks->elements(nsP(), u"par")) {
        collectEntrances(*click, ++step, builds);
    }
    return builds;
}

namespace {

// Builders for p:timing: every time node needs a unique id.
class TimingBuilder {
public:
    Ooxml::XmlElement &cTn(Ooxml::XmlElement &parent)
    {
        Ooxml::XmlElement &node = parent.appendElement(nsP(), QStringLiteral("cTn"));
        node.setAttribute(QString(), QStringLiteral("id"), QString::number(++m_id));
        return node;
    }
    static void delay(Ooxml::XmlElement &cTn, const QString &value)
    {
        cTn.appendElement(nsP(), QStringLiteral("stCondLst"))
            .appendElement(nsP(), QStringLiteral("cond"))
            .setAttribute(QString(), QStringLiteral("delay"), value);
    }
    void behaviour(Ooxml::XmlElement &parent, quint32 shapeId, int duration, const QString &attribute = {})
    {
        Ooxml::XmlElement &cBhvr = parent.appendElement(nsP(), QStringLiteral("cBhvr"));
        Ooxml::XmlElement &node = cTn(cBhvr);
        node.setAttribute(QString(), QStringLiteral("dur"), QString::number(qMax(1, duration)));
        node.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
        if (duration <= 1) {
            delay(node, QStringLiteral("0"));
        }
        cBhvr.appendElement(nsP(), QStringLiteral("tgtEl"))
            .appendElement(nsP(), QStringLiteral("spTgt"))
            .setAttribute(QString(), QStringLiteral("spid"), QString::number(shapeId));
        if (!attribute.isEmpty()) {
            cBhvr.appendElement(nsP(), QStringLiteral("attrNameLst"))
                .appendElement(nsP(), QStringLiteral("attrName"))
                .appendText(attribute);
        }
    }
    // An animated property from `from` to its own value (ppt_x / ppt_y / ppt_w / ppt_h).
    void slide(Ooxml::XmlElement &list, quint32 shapeId, int duration, const QString &attribute, const QString &from)
    {
        Ooxml::XmlElement &anim = list.appendElement(nsP(), QStringLiteral("anim"));
        anim.setAttribute(QString(), QStringLiteral("calcmode"), QStringLiteral("lin"));
        anim.setAttribute(QString(), QStringLiteral("valueType"), QStringLiteral("num"));
        behaviour(anim, shapeId, duration, attribute);
        anim.child(nsP(), u"cBhvr")->setAttribute(QString(), QStringLiteral("additive"), QStringLiteral("base"));
        Ooxml::XmlElement &values = anim.appendElement(nsP(), QStringLiteral("tavLst"));
        for (const auto &[time, value] : {std::pair{QStringLiteral("0"), from}, std::pair{QStringLiteral("100000"), QStringLiteral("#") + attribute}}) {
            Ooxml::XmlElement &tav = values.appendElement(nsP(), QStringLiteral("tav"));
            tav.setAttribute(QString(), QStringLiteral("tm"), time);
            tav.appendElement(nsP(), QStringLiteral("val"))
                .appendElement(nsP(), QStringLiteral("strVal"))
                .setAttribute(QString(), QStringLiteral("val"), value);
        }
    }
    void effect(Ooxml::XmlElement &list, const BuildEntry &build, bool first)
    {
        Ooxml::XmlElement &par = list.appendElement(nsP(), QStringLiteral("par"));
        Ooxml::XmlElement &node = cTn(par);
        int preset = 1;
        int subtype = 0;
        switch (build.animation) {
        case ShapeAnimation::Appear: preset = 1; break;
        case ShapeAnimation::Fade: preset = 10; break;
        case ShapeAnimation::FlyInLeft: preset = 2; subtype = 8; break;
        case ShapeAnimation::FlyInRight: preset = 2; subtype = 2; break;
        case ShapeAnimation::FlyInTop: preset = 2; subtype = 1; break;
        case ShapeAnimation::FlyInBottom: preset = 2; subtype = 4; break;
        case ShapeAnimation::Zoom: preset = 53; subtype = 16; break;
        }
        node.setAttribute(QString(), QStringLiteral("presetID"), QString::number(preset));
        node.setAttribute(QString(), QStringLiteral("presetClass"), QStringLiteral("entr"));
        node.setAttribute(QString(), QStringLiteral("presetSubtype"), QString::number(subtype));
        node.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
        node.setAttribute(QString(), QStringLiteral("grpId"), QStringLiteral("0"));
        node.setAttribute(QString(), QStringLiteral("nodeType"), first ? QStringLiteral("clickEffect") : QStringLiteral("withEffect"));
        delay(node, QStringLiteral("0"));
        Ooxml::XmlElement &children = node.appendElement(nsP(), QStringLiteral("childTnLst"));
        Ooxml::XmlElement &set = children.appendElement(nsP(), QStringLiteral("set"));
        behaviour(set, build.shapeId, 1, QStringLiteral("style.visibility"));
        set.appendElement(nsP(), QStringLiteral("to"))
            .appendElement(nsP(), QStringLiteral("strVal"))
            .setAttribute(QString(), QStringLiteral("val"), QStringLiteral("visible"));
        const int duration = build.durationMs > 0 ? build.durationMs : 500;
        if (preset == 10 || preset == 53) {
            Ooxml::XmlElement &fade = children.appendElement(nsP(), QStringLiteral("animEffect"));
            fade.setAttribute(QString(), QStringLiteral("transition"), QStringLiteral("in"));
            fade.setAttribute(QString(), QStringLiteral("filter"), QStringLiteral("fade"));
            behaviour(fade, build.shapeId, duration);
        }
        if (preset == 53) {
            slide(children, build.shapeId, duration, QStringLiteral("ppt_w"), QStringLiteral("0"));
            slide(children, build.shapeId, duration, QStringLiteral("ppt_h"), QStringLiteral("0"));
        } else if (preset == 2) {
            const bool horizontal = subtype == 2 || subtype == 8;
            const QString from = subtype == 8 ? QStringLiteral("0-#ppt_w/2")
                : subtype == 2               ? QStringLiteral("1+#ppt_w/2")
                : subtype == 1               ? QStringLiteral("0-#ppt_h/2")
                                             : QStringLiteral("1+#ppt_h/2");
            slide(children, build.shapeId, duration, horizontal ? QStringLiteral("ppt_x") : QStringLiteral("ppt_y"), from);
        }
    }

private:
    int m_id = 0;
};

} // namespace

Ooxml::XmlElementPtr timingElement(const QList<BuildEntry> &builds)
{
    if (builds.isEmpty()) {
        return nullptr;
    }
    QList<BuildEntry> ordered = builds;
    std::stable_sort(ordered.begin(), ordered.end(), [](const BuildEntry &a, const BuildEntry &b) { return a.step < b.step; });
    TimingBuilder b;
    auto timing = std::make_shared<Ooxml::XmlElement>(nsP(), QStringLiteral("timing"));
    Ooxml::XmlElement &rootPar = timing->appendElement(nsP(), QStringLiteral("tnLst")).appendElement(nsP(), QStringLiteral("par"));
    Ooxml::XmlElement &root = b.cTn(rootPar);
    root.setAttribute(QString(), QStringLiteral("dur"), QStringLiteral("indefinite"));
    root.setAttribute(QString(), QStringLiteral("restart"), QStringLiteral("never"));
    root.setAttribute(QString(), QStringLiteral("nodeType"), QStringLiteral("tmRoot"));
    Ooxml::XmlElement &seq = root.appendElement(nsP(), QStringLiteral("childTnLst")).appendElement(nsP(), QStringLiteral("seq"));
    seq.setAttribute(QString(), QStringLiteral("concurrent"), QStringLiteral("1"));
    seq.setAttribute(QString(), QStringLiteral("nextAc"), QStringLiteral("seek"));
    Ooxml::XmlElement &main = b.cTn(seq);
    main.setAttribute(QString(), QStringLiteral("dur"), QStringLiteral("indefinite"));
    main.setAttribute(QString(), QStringLiteral("nodeType"), QStringLiteral("mainSeq"));
    Ooxml::XmlElement &clicks = main.appendElement(nsP(), QStringLiteral("childTnLst"));
    for (qsizetype i = 0; i < ordered.size();) {
        const int step = ordered.at(i).step;
        Ooxml::XmlElement &click = b.cTn(clicks.appendElement(nsP(), QStringLiteral("par")));
        click.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
        TimingBuilder::delay(click, QStringLiteral("indefinite"));
        Ooxml::XmlElement &group = b.cTn(click.appendElement(nsP(), QStringLiteral("childTnLst")).appendElement(nsP(), QStringLiteral("par")));
        group.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
        TimingBuilder::delay(group, QStringLiteral("0"));
        Ooxml::XmlElement &effects = group.appendElement(nsP(), QStringLiteral("childTnLst"));
        bool first = true;
        for (; i < ordered.size() && ordered.at(i).step == step; ++i) {
            b.effect(effects, ordered.at(i), first);
            first = false;
        }
    }
    for (const char16_t *list : {u"prevCondLst", u"nextCondLst"}) {
        Ooxml::XmlElement &cond = seq.appendElement(nsP(), QString::fromUtf16(list)).appendElement(nsP(), QStringLiteral("cond"));
        cond.setAttribute(QString(), QStringLiteral("evt"), list == std::u16string_view(u"prevCondLst") ? QStringLiteral("onPrev") : QStringLiteral("onNext"));
        cond.setAttribute(QString(), QStringLiteral("delay"), QStringLiteral("0"));
        cond.appendElement(nsP(), QStringLiteral("tgtEl")).appendElement(nsP(), QStringLiteral("sldTgt"));
    }
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
