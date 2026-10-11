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
    // One effect of the main sequence (pptx_effects.cpp).
    void effect(Ooxml::XmlElement &list, const BuildEntry &build, bool first);

private:
    int m_id = 0;
};

// An entrance, emphasis or exit effect as PowerPoint writes its preset —
// the M91 Effect Options direction (BuildEntry::direction) and exits
// included (pptx_effects.cpp).
void appendEffect(TimingBuilder &b, Ooxml::XmlElement &list, const BuildEntry &build, bool first);

inline void TimingBuilder::effect(Ooxml::XmlElement &list, const BuildEntry &build, bool first)
{
    appendEffect(*this, list, build, first);
}

// ---- media (pptx_timing_media.cpp, M75) ----
// The main sequence's first group, starting with the slide (its condition
// names the main sequence's time node `mainId`): `media` played in turn.
void appendAutoPlay(TimingBuilder &b, Ooxml::XmlElement &clicks, const QString &mainId, const QList<MediaEntry> &media);
// One media shape's play command as a click (or with-click) effect.
void appendMediaCall(TimingBuilder &b, Ooxml::XmlElement &effects, const MediaEntry &media, bool first);
// The p:video / p:audio media node of every media shape.
void appendMediaNodes(TimingBuilder &b, Ooxml::XmlElement &rootChildren, const QList<MediaEntry> &media);

} // namespace QindaOffice::Show::Pptx
