// SPDX-License-Identifier: GPL-3.0-or-later
// One entrance, emphasis or exit effect of the main sequence as PowerPoint
// writes its presets (pptx_timing_builder.h; M67, D-076; M91, D-099: the
// Effect Options direction — presetSubtype — and exit effects, Peek, Float).
//
//   preset   entrance (class entr)              exit (class exit)
//   1        Appear                             Disappear
//   10       Fade                               Fade
//   2        Fly In from <dir>: ppt_x / ppt_y   Fly Out to <dir>
//   53       Zoom: fade + ppt_w / ppt_h from 0  Zoom: to 0
//   22       Wipe: animEffect wipe(<dir>)       Wipe (out)
//   16       Split: animEffect barn(<orient>)   Split (out)
//   12       Peek In: slide + wipe              Peek Out
//   42 / 47  Float In up / down: fade + glide   Float Out up / down
//   8, 6     Spin, Grow/Shrink (class emph)

#include "pptx_timing_builder.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;

struct Preset {
    int id = 1;
    int subtype = 0;
};

Preset presetOf(const BuildEntry &build)
{
    const int direction = effectiveDirection(build.animation, build.direction);
    switch (build.animation) {
    case ShapeAnimation::Appear: return {1, 0};
    case ShapeAnimation::Fade: return {10, 0};
    case ShapeAnimation::FlyInLeft:
    case ShapeAnimation::FlyInRight:
    case ShapeAnimation::FlyInTop:
    case ShapeAnimation::FlyInBottom: return {2, direction};
    case ShapeAnimation::Zoom: return {53, 16};
    case ShapeAnimation::Wipe: return {22, direction};
    case ShapeAnimation::Split: return {16, direction};
    case ShapeAnimation::Spin: return {8, 0};
    case ShapeAnimation::GrowShrink: return {6, 0};
    case ShapeAnimation::Peek: return {12, direction};
    case ShapeAnimation::Float: return {direction == Direction::Bottom ? 47 : 42, 0};
    }
    return {};
}

// The animEffect filter of a wipe starting at the `direction` edge.
QString wipeFilter(int direction)
{
    switch (direction) {
    case Direction::Top: return QStringLiteral("wipe(down)");
    case Direction::Left: return QStringLiteral("wipe(right)");
    case Direction::Right: return QStringLiteral("wipe(left)");
    default: return QStringLiteral("wipe(up)");
    }
}

QString splitFilter(int direction)
{
    switch (direction) {
    case Direction::HorizontalIn: return QStringLiteral("barn(inHorizontal)");
    case Direction::VerticalIn: return QStringLiteral("barn(inVertical)");
    case Direction::HorizontalOut: return QStringLiteral("barn(outHorizontal)");
    default: return QStringLiteral("barn(outVertical)");
    }
}

// An animated property from `from` to `to` (formulas PowerPoint evaluates:
// "#ppt_x" is the shape's own value, "ppt_x" the value now).
void anim(TimingBuilder &b, XmlElement &list, quint32 shapeId, int duration, const QString &attribute, const QString &from,
          const QString &to)
{
    XmlElement &animation = list.appendElement(nsP(), QStringLiteral("anim"));
    animation.setAttribute(QString(), QStringLiteral("calcmode"), QStringLiteral("lin"));
    animation.setAttribute(QString(), QStringLiteral("valueType"), QStringLiteral("num"));
    b.behaviour(animation, shapeId, duration, attribute);
    animation.child(nsP(), u"cBhvr")->setAttribute(QString(), QStringLiteral("additive"), QStringLiteral("base"));
    XmlElement &values = animation.appendElement(nsP(), QStringLiteral("tavLst"));
    for (const auto &[time, value] : {std::pair{QStringLiteral("0"), from}, std::pair{QStringLiteral("100000"), to}}) {
        XmlElement &tav = values.appendElement(nsP(), QStringLiteral("tav"));
        tav.setAttribute(QString(), QStringLiteral("tm"), time);
        tav.appendElement(nsP(), QStringLiteral("val"))
            .appendElement(nsP(), QStringLiteral("strVal"))
            .setAttribute(QString(), QStringLiteral("val"), value);
    }
}

void filter(TimingBuilder &b, XmlElement &list, quint32 shapeId, int duration, const QString &name, bool in)
{
    XmlElement &effect = list.appendElement(nsP(), QStringLiteral("animEffect"));
    effect.setAttribute(QString(), QStringLiteral("transition"), in ? QStringLiteral("in") : QStringLiteral("out"));
    effect.setAttribute(QString(), QStringLiteral("filter"), name);
    b.behaviour(effect, shapeId, duration);
}

void visibility(TimingBuilder &b, XmlElement &list, quint32 shapeId, bool visible, int delay)
{
    XmlElement &set = list.appendElement(nsP(), QStringLiteral("set"));
    b.behaviour(set, shapeId, 1, QStringLiteral("style.visibility"));
    if (delay > 0) {
        XmlElement *node = set.child(nsP(), u"cBhvr")->child(nsP(), u"cTn");
        node->removeChildren(nsP(), u"stCondLst");
        TimingBuilder::delay(*node, QString::number(delay));
    }
    set.appendElement(nsP(), QStringLiteral("to"))
        .appendElement(nsP(), QStringLiteral("strVal"))
        .setAttribute(QString(), QStringLiteral("val"), visible ? QStringLiteral("visible") : QStringLiteral("hidden"));
}

// Fly In from / Fly Out to `direction`: the off-slide end of the move.
void fly(TimingBuilder &b, XmlElement &list, quint32 id, int duration, int direction, bool in)
{
    const bool left = (direction & Direction::Left) != 0;
    const bool right = (direction & Direction::Right) != 0;
    const bool top = (direction & Direction::Top) != 0;
    const bool bottom = (direction & Direction::Bottom) != 0;
    const QString hash = in ? QStringLiteral("#") : QString();
    const QString x = left ? QStringLiteral("0-%1ppt_w/2").arg(hash) : right ? QStringLiteral("1+%1ppt_w/2").arg(hash) : QString();
    const QString y = top ? QStringLiteral("0-%1ppt_h/2").arg(hash) : bottom ? QStringLiteral("1+%1ppt_h/2").arg(hash) : QString();
    const QString ownX = hash + QStringLiteral("ppt_x");
    const QString ownY = hash + QStringLiteral("ppt_y");
    if (!x.isEmpty() || y.isEmpty()) {
        anim(b, list, id, duration, QStringLiteral("ppt_x"), in ? (x.isEmpty() ? ownX : x) : ownX, in ? ownX : (x.isEmpty() ? ownX : x));
    }
    if (!y.isEmpty()) {
        anim(b, list, id, duration, QStringLiteral("ppt_y"), in ? y : ownY, in ? ownY : y);
    }
}

// Peek: a short slide from (to) the edge, clipped by a wipe.
void peek(TimingBuilder &b, XmlElement &list, quint32 id, int duration, int direction, bool in)
{
    const bool horizontal = direction == Direction::Left || direction == Direction::Right;
    const QString attribute = horizontal ? QStringLiteral("ppt_x") : QStringLiteral("ppt_y");
    const QString size = horizontal ? QStringLiteral("ppt_w") : QStringLiteral("ppt_h");
    const QChar sign = direction == Direction::Left || direction == Direction::Top ? QLatin1Char('-') : QLatin1Char('+');
    if (in) {
        anim(b, list, id, duration, attribute, QStringLiteral("#%1%2#%3*1.125000").arg(attribute, sign, size),
             QLatin1Char('#') + attribute);
    } else {
        anim(b, list, id, duration, attribute, attribute, QStringLiteral("%1%2%3").arg(attribute, sign, size));
    }
    filter(b, list, id, duration, wipeFilter(direction), in);
}

} // namespace

void appendEffect(TimingBuilder &b, XmlElement &list, const BuildEntry &build, bool first)
{
    XmlElement &par = list.appendElement(nsP(), QStringLiteral("par"));
    XmlElement &node = b.cTn(par);
    const Preset preset = presetOf(build);
    const bool emphasis = isEmphasis(build.animation) && !build.exit;
    node.setAttribute(QString(), QStringLiteral("presetID"), QString::number(preset.id));
    node.setAttribute(QString(), QStringLiteral("presetClass"), emphasis ? QStringLiteral("emph")
                                                                : build.exit ? QStringLiteral("exit")
                                                                             : QStringLiteral("entr"));
    node.setAttribute(QString(), QStringLiteral("presetSubtype"), QString::number(preset.subtype));
    node.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
    // An exit is the shape's second build (grpId 1; its entrance is 0).
    node.setAttribute(QString(), QStringLiteral("grpId"), build.exit ? QStringLiteral("1") : QStringLiteral("0"));
    node.setAttribute(QString(), QStringLiteral("nodeType"), first ? QStringLiteral("clickEffect") : QStringLiteral("withEffect"));
    TimingBuilder::delay(node, QStringLiteral("0"));
    XmlElement &children = node.appendElement(nsP(), QStringLiteral("childTnLst"));
    const quint32 id = build.shapeId;
    if (emphasis) {
        // On the slide already: it only turns (r, a full turn in
        // 60000ths of a degree) or scales (to 150 %).
        const int duration = build.durationMs > 0 ? build.durationMs : 2000;
        if (build.animation == ShapeAnimation::Spin) {
            XmlElement &rotate = children.appendElement(nsP(), QStringLiteral("animRot"));
            rotate.setAttribute(QString(), QStringLiteral("by"), QStringLiteral("21600000"));
            b.behaviour(rotate, id, duration, QStringLiteral("r"));
        } else {
            XmlElement &scale = children.appendElement(nsP(), QStringLiteral("animScale"));
            b.behaviour(scale, id, duration);
            XmlElement &by = scale.appendElement(nsP(), QStringLiteral("by"));
            by.setAttribute(QString(), QStringLiteral("x"), QStringLiteral("150000"));
            by.setAttribute(QString(), QStringLiteral("y"), QStringLiteral("150000"));
        }
        return;
    }
    const bool in = !build.exit;
    const int duration = build.durationMs > 0 ? build.durationMs : (preset.id == 42 || preset.id == 47 ? 1000 : 500);
    if (in) {
        visibility(b, children, id, true, 0);
    }
    const int direction = effectiveDirection(build.animation, build.direction);
    switch (preset.id) {
    case 1:
        break;
    case 10:
        filter(b, children, id, duration, QStringLiteral("fade"), in);
        break;
    case 53:
        filter(b, children, id, duration, QStringLiteral("fade"), in);
        for (const char *size : {"ppt_w", "ppt_h"}) {
            const QString attribute = QLatin1String(size);
            anim(b, children, id, duration, attribute, in ? QStringLiteral("0") : attribute, in ? QLatin1Char('#') + attribute : QStringLiteral("0"));
        }
        break;
    case 2:
        fly(b, children, id, duration, direction, in);
        break;
    case 22:
        filter(b, children, id, duration, wipeFilter(direction), in);
        break;
    case 16:
        filter(b, children, id, duration, splitFilter(direction), in);
        break;
    case 12:
        peek(b, children, id, duration, direction, in);
        break;
    case 42:
    case 47: {
        filter(b, children, id, duration, QStringLiteral("fade"), in);
        const QString offset = preset.id == 42 ? (in ? QStringLiteral("+.1") : QStringLiteral("-.1"))
                                               : (in ? QStringLiteral("-.1") : QStringLiteral("+.1"));
        anim(b, children, id, duration, QStringLiteral("ppt_y"), in ? QStringLiteral("#ppt_y") + offset : QStringLiteral("ppt_y"),
             in ? QStringLiteral("#ppt_y") : QStringLiteral("ppt_y") + offset);
        break;
    }
    default:
        break;
    }
    if (!in) {
        visibility(b, children, id, false, preset.id == 1 ? 0 : qMax(0, duration - 1));
    }
}

} // namespace QindaOffice::Show::Pptx
