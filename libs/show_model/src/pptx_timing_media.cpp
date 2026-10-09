// SPDX-License-Identifier: GPL-3.0-or-later
// Media playback in p:timing (M75, D-084; pptx_timing.h): PowerPoint
// spells a video's or a sound's playback in two places — a media node
// (p:video / p:audio > p:cMediaNode: volume, Hide While Not Playing as
// showWhenStopped="0", Loop until Stopped as the node's
// repeatCount="indefinite") and, unless it only plays when clicked on, a
// "mediacall" effect in the main sequence whose p:cmd plays it: in the
// sequence's first group, which starts with the slide (Start
// Automatically), or as a click effect (Start On Click).

#include "pptx_timing.h"

#include "pptx_timing_builder.h"

#include <functional>

namespace QindaOffice::Show::Pptx {

namespace {

void set(Ooxml::XmlElement &e, const char *name, const QString &value)
{
    e.setAttribute(QString(), QLatin1String(name), value);
}

quint32 targetOf(const Ooxml::XmlElement &e)
{
    const Ooxml::XmlElement *target = e.descendant(nsP(), u"spTgt");
    return target != nullptr ? target->attr(u"spid").toUInt() : 0;
}

// Play commands under `node` (mediacall effects), in document order.
void collectCalls(const Ooxml::XmlElement &node, QList<std::pair<quint32, int>> &calls)
{
    for (const Ooxml::XmlElement *child : node.elements()) {
        if (child->is(nsP(), u"cTn") && child->attr(u"presetClass") == u"mediacall") {
            if (const Ooxml::XmlElement *cmd = child->descendant(nsP(), u"cmd")) {
                const QString verb = cmd->attr(u"cmd");
                if (verb.startsWith(QLatin1String("playFrom")) || verb == u"togglePause" || verb == u"play") {
                    const Ooxml::XmlElement *behaviour = cmd->child(nsP(), u"cBhvr");
                    const Ooxml::XmlElement *timed = behaviour != nullptr ? behaviour->child(nsP(), u"cTn") : nullptr;
                    calls.append({targetOf(*cmd), timed != nullptr ? timed->attr(u"dur").toInt() : 0});
                }
            }
            continue;
        }
        collectCalls(*child, calls);
    }
}

MediaEntry *entryFor(QList<MediaEntry> &media, quint32 shapeId)
{
    for (MediaEntry &entry : media) {
        if (entry.shapeId == shapeId) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

QList<MediaEntry> readMedia(const Ooxml::XmlElement *timing)
{
    QList<MediaEntry> media;
    if (timing == nullptr) {
        return media;
    }
    // The media nodes, wherever the root's children hold them.
    std::function<void(const Ooxml::XmlElement &)> nodes = [&](const Ooxml::XmlElement &e) {
        for (const Ooxml::XmlElement *child : e.elements()) {
            if (child->is(nsP(), u"video") || child->is(nsP(), u"audio")) {
                const Ooxml::XmlElement *node = child->child(nsP(), u"cMediaNode");
                if (node == nullptr) {
                    continue;
                }
                MediaEntry entry;
                entry.kind = child->name == u"video" ? MediaKind::Video : MediaKind::Audio;
                entry.shapeId = targetOf(*node);
                entry.volume = qBound(0, int(node->attr(u"vol", QStringLiteral("50000")).toInt() / 1000), 100);
                entry.hideWhenStopped = node->attr(u"showWhenStopped") == u"0" || node->attr(u"showWhenStopped") == u"false";
                const Ooxml::XmlElement *cTn = node->child(nsP(), u"cTn");
                entry.loop = cTn != nullptr && cTn->attr(u"repeatCount") == u"indefinite";
                if (entry.shapeId != 0 && entryFor(media, entry.shapeId) == nullptr) {
                    media.append(entry);
                }
                continue;
            }
            nodes(*child);
        }
    };
    nodes(*timing);
    // The main sequence: a play command in a group that starts with the
    // slide plays automatically; any other plays at its click.
    const Ooxml::XmlElement *seq = timing->descendant(nsP(), u"seq");
    const Ooxml::XmlElement *main = seq != nullptr ? seq->child(nsP(), u"cTn") : nullptr;
    const Ooxml::XmlElement *clicks = main != nullptr && main->attr(u"nodeType") == u"mainSeq"
        ? main->child(nsP(), u"childTnLst")
        : nullptr;
    if (clicks == nullptr) {
        return media;
    }
    const QList<const Ooxml::XmlElement *> pars = clicks->elements(nsP(), u"par");
    const QList<int> steps = clickSteps(*clicks);
    for (qsizetype i = 0; i < pars.size(); ++i) {
        QList<std::pair<quint32, int>> calls;
        collectCalls(*pars.at(i), calls);
        for (const auto &[shapeId, duration] : std::as_const(calls)) {
            MediaEntry *entry = entryFor(media, shapeId);
            if (entry == nullptr) {
                continue;
            }
            if (startsWithSlide(*pars.at(i))) {
                entry->autoPlay = true;
            } else if (entry->step == 0) {
                entry->step = steps.at(i);
            }
            entry->durationMs = qMax(entry->durationMs, duration);
        }
    }
    return media;
}

void appendMediaCall(TimingBuilder &b, Ooxml::XmlElement &effects, const MediaEntry &media, bool first)
{
    Ooxml::XmlElement &par = effects.appendElement(nsP(), QStringLiteral("par"));
    Ooxml::XmlElement &node = b.cTn(par);
    set(node, "presetID", QStringLiteral("1"));
    set(node, "presetClass", QStringLiteral("mediacall"));
    set(node, "presetSubtype", QStringLiteral("0"));
    set(node, "fill", QStringLiteral("hold"));
    const QString order = media.autoPlay ? QStringLiteral("afterEffect") : QStringLiteral("clickEffect");
    set(node, "nodeType", first ? order : QStringLiteral("withEffect"));
    TimingBuilder::delay(node, QStringLiteral("0"));
    Ooxml::XmlElement &cmd = node.appendElement(nsP(), QStringLiteral("childTnLst")).appendElement(nsP(), QStringLiteral("cmd"));
    set(cmd, "type", QStringLiteral("call"));
    set(cmd, "cmd", QStringLiteral("playFrom(0.0)"));
    Ooxml::XmlElement &behaviour = cmd.appendElement(nsP(), QStringLiteral("cBhvr"));
    Ooxml::XmlElement &timed = b.cTn(behaviour);
    set(timed, "dur", media.durationMs > 0 ? QString::number(media.durationMs) : QStringLiteral("indefinite"));
    set(timed, "fill", QStringLiteral("hold"));
    set(behaviour.appendElement(nsP(), QStringLiteral("tgtEl")).appendElement(nsP(), QStringLiteral("spTgt")), "spid",
        QString::number(media.shapeId));
}

void appendAutoPlay(TimingBuilder &b, Ooxml::XmlElement &clicks, const QString &mainId, const QList<MediaEntry> &media)
{
    Ooxml::XmlElement &click = b.cTn(clicks.appendElement(nsP(), QStringLiteral("par")));
    set(click, "fill", QStringLiteral("hold"));
    Ooxml::XmlElement &conditions = click.appendElement(nsP(), QStringLiteral("stCondLst"));
    set(conditions.appendElement(nsP(), QStringLiteral("cond")), "delay", QStringLiteral("indefinite"));
    Ooxml::XmlElement &begin = conditions.appendElement(nsP(), QStringLiteral("cond"));
    set(begin, "evt", QStringLiteral("onBegin"));
    set(begin, "delay", QStringLiteral("0"));
    set(begin.appendElement(nsP(), QStringLiteral("tn")), "val", mainId);
    Ooxml::XmlElement &group = b.cTn(click.appendElement(nsP(), QStringLiteral("childTnLst")).appendElement(nsP(), QStringLiteral("par")));
    set(group, "fill", QStringLiteral("hold"));
    TimingBuilder::delay(group, QStringLiteral("0"));
    Ooxml::XmlElement &effects = group.appendElement(nsP(), QStringLiteral("childTnLst"));
    bool first = true;
    for (const MediaEntry &entry : media) {
        appendMediaCall(b, effects, entry, first);
        first = false;
    }
}

void appendMediaNodes(TimingBuilder &b, Ooxml::XmlElement &rootChildren, const QList<MediaEntry> &media)
{
    for (const MediaEntry &entry : media) {
        Ooxml::XmlElement &kind = rootChildren.appendElement(
            nsP(), entry.kind == MediaKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
        Ooxml::XmlElement &node = kind.appendElement(nsP(), QStringLiteral("cMediaNode"));
        set(node, "vol", QString::number(qBound(0, entry.volume, 100) * 1000));
        if (entry.hideWhenStopped) {
            set(node, "showWhenStopped", QStringLiteral("0"));
        }
        Ooxml::XmlElement &cTn = b.cTn(node);
        if (entry.loop) {
            set(cTn, "repeatCount", QStringLiteral("indefinite"));
        }
        set(cTn, "fill", QStringLiteral("hold"));
        set(cTn, "display", QStringLiteral("0"));
        TimingBuilder::delay(cTn, QStringLiteral("indefinite"));
        set(node.appendElement(nsP(), QStringLiteral("tgtEl")).appendElement(nsP(), QStringLiteral("spTgt")), "spid",
            QString::number(entry.shapeId));
    }
}

} // namespace QindaOffice::Show::Pptx
