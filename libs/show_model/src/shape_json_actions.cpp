// SPDX-License-Identifier: GPL-3.0-or-later
// The shape clipboard codec's M91 pieces (shape_json_parts.h): action
// settings, exit effects and a medium's trim, fades and bookmarks.

#include "shape_json_parts.h"

namespace QindaOffice::Show::Json {

QJsonObject action(const ShowAction &a)
{
    QJsonObject o{{QStringLiteral("link"), a.link}};
    if (!a.address.isEmpty()) {
        o.insert(QStringLiteral("address"), a.address);
    }
    if (!a.tooltip.isEmpty()) {
        o.insert(QStringLiteral("tooltip"), a.tooltip);
    }
    if (a.highlight) {
        o.insert(QStringLiteral("highlight"), true);
    }
    if (a.stopSound) {
        o.insert(QStringLiteral("stopSound"), true);
    }
    if (!a.sound.isEmpty()) {
        o.insert(QStringLiteral("sound"), QString::fromLatin1(a.sound.toBase64()));
        o.insert(QStringLiteral("soundName"), a.soundName);
    }
    return o;
}

ShowAction action(const QJsonObject &o)
{
    ShowAction a;
    a.link = o.value(QStringLiteral("link")).toString();
    a.address = o.value(QStringLiteral("address")).toString();
    a.tooltip = o.value(QStringLiteral("tooltip")).toString();
    a.highlight = o.value(QStringLiteral("highlight")).toBool();
    a.stopSound = o.value(QStringLiteral("stopSound")).toBool();
    a.sound = QByteArray::fromBase64(o.value(QStringLiteral("sound")).toString().toLatin1());
    a.soundName = o.value(QStringLiteral("soundName")).toString();
    return a;
}

QJsonObject exit(const ShapeExit &e)
{
    return {{QStringLiteral("step"), e.step},
            {QStringLiteral("animation"), int(e.animation)},
            {QStringLiteral("direction"), e.direction},
            {QStringLiteral("durationMs"), e.durationMs}};
}

ShapeExit exit(const QJsonObject &o)
{
    ShapeExit e;
    e.step = qMax(0, o.value(QStringLiteral("step")).toInt());
    e.animation = ShapeAnimation(qBound(0, o.value(QStringLiteral("animation")).toInt(int(ShapeAnimation::Fade)),
                                        int(kLastAnimation)));
    e.direction = o.value(QStringLiteral("direction")).toInt();
    e.durationMs = qBound(1, o.value(QStringLiteral("durationMs")).toInt(500), 60000);
    return e;
}

void mediaEdits(QJsonObject &into, const MediaInfo &m)
{
    if (m.trimStartMs != 0 || m.trimEndMs != 0) {
        into.insert(QStringLiteral("trim"), QJsonArray{m.trimStartMs, m.trimEndMs});
    }
    if (m.fadeInMs != 0 || m.fadeOutMs != 0) {
        into.insert(QStringLiteral("fade"), QJsonArray{m.fadeInMs, m.fadeOutMs});
    }
    if (!m.bookmarks.isEmpty()) {
        QJsonArray bookmarks;
        for (const MediaBookmark &b : m.bookmarks) {
            bookmarks.append(QJsonArray{b.name, b.timeMs});
        }
        into.insert(QStringLiteral("bookmarks"), bookmarks);
    }
}

void mediaEdits(const QJsonObject &from, MediaInfo &m)
{
    const QJsonArray trim = from.value(QStringLiteral("trim")).toArray();
    m.trimStartMs = qMax(0, trim.at(0).toInt());
    m.trimEndMs = qMax(0, trim.at(1).toInt());
    const QJsonArray fade = from.value(QStringLiteral("fade")).toArray();
    m.fadeInMs = qMax(0, fade.at(0).toInt());
    m.fadeOutMs = qMax(0, fade.at(1).toInt());
    for (const QJsonValue &value : from.value(QStringLiteral("bookmarks")).toArray()) {
        const QJsonArray pair = value.toArray();
        m.bookmarks.append({pair.at(0).toString(), qMax(0, pair.at(1).toInt())});
    }
}

} // namespace QindaOffice::Show::Json
