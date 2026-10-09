// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The p:timing builder shared by the build writer (pptx_timing.cpp) and the
// media writer (pptx_timing_media.cpp; M75, D-084). Internal to show_model.

#include "pptx_timing.h"

#include <string_view>

namespace QindaOffice::Show::Pptx {

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
        case ShapeAnimation::Wipe: preset = 22; subtype = 4; break;   // from bottom
        case ShapeAnimation::Split: preset = 16; subtype = 37; break; // vertical out
        case ShapeAnimation::Spin: preset = 8; break;
        case ShapeAnimation::GrowShrink: preset = 6; break;
        }
        const bool emphasis = isEmphasis(build.animation);
        node.setAttribute(QString(), QStringLiteral("presetID"), QString::number(preset));
        node.setAttribute(QString(), QStringLiteral("presetClass"), emphasis ? QStringLiteral("emph") : QStringLiteral("entr"));
        node.setAttribute(QString(), QStringLiteral("presetSubtype"), QString::number(subtype));
        node.setAttribute(QString(), QStringLiteral("fill"), QStringLiteral("hold"));
        node.setAttribute(QString(), QStringLiteral("grpId"), QStringLiteral("0"));
        node.setAttribute(QString(), QStringLiteral("nodeType"), first ? QStringLiteral("clickEffect") : QStringLiteral("withEffect"));
        delay(node, QStringLiteral("0"));
        Ooxml::XmlElement &children = node.appendElement(nsP(), QStringLiteral("childTnLst"));
        if (emphasis) {
            // On the slide already: it only turns (r, a full turn in
            // 60000ths of a degree) or scales (to 150 %).
            const int duration = build.durationMs > 0 ? build.durationMs : 2000;
            if (build.animation == ShapeAnimation::Spin) {
                Ooxml::XmlElement &rotate = children.appendElement(nsP(), QStringLiteral("animRot"));
                rotate.setAttribute(QString(), QStringLiteral("by"), QStringLiteral("21600000"));
                behaviour(rotate, build.shapeId, duration, QStringLiteral("r"));
            } else {
                Ooxml::XmlElement &scale = children.appendElement(nsP(), QStringLiteral("animScale"));
                behaviour(scale, build.shapeId, duration);
                Ooxml::XmlElement &by = scale.appendElement(nsP(), QStringLiteral("by"));
                by.setAttribute(QString(), QStringLiteral("x"), QStringLiteral("150000"));
                by.setAttribute(QString(), QStringLiteral("y"), QStringLiteral("150000"));
            }
            return;
        }
        Ooxml::XmlElement &set = children.appendElement(nsP(), QStringLiteral("set"));
        behaviour(set, build.shapeId, 1, QStringLiteral("style.visibility"));
        set.appendElement(nsP(), QStringLiteral("to"))
            .appendElement(nsP(), QStringLiteral("strVal"))
            .setAttribute(QString(), QStringLiteral("val"), QStringLiteral("visible"));
        const int duration = build.durationMs > 0 ? build.durationMs : 500;
        if (preset == 22 || preset == 16) {
            Ooxml::XmlElement &reveal = children.appendElement(nsP(), QStringLiteral("animEffect"));
            reveal.setAttribute(QString(), QStringLiteral("transition"), QStringLiteral("in"));
            reveal.setAttribute(QString(), QStringLiteral("filter"), preset == 22 ? QStringLiteral("wipe(up)") : QStringLiteral("barn(outVertical)"));
            behaviour(reveal, build.shapeId, duration);
        }
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

// ---- media (pptx_timing_media.cpp, M75) ----
// The main sequence's first group, starting with the slide (its condition
// names the main sequence's time node `mainId`): `media` played in turn.
void appendAutoPlay(TimingBuilder &b, Ooxml::XmlElement &clicks, const QString &mainId, const QList<MediaEntry> &media);
// One media shape's play command as a click (or with-click) effect.
void appendMediaCall(TimingBuilder &b, Ooxml::XmlElement &effects, const MediaEntry &media, bool first);
// The p:video / p:audio media node of every media shape.
void appendMediaNodes(TimingBuilder &b, Ooxml::XmlElement &rootChildren, const QList<MediaEntry> &media);

} // namespace QindaOffice::Show::Pptx
