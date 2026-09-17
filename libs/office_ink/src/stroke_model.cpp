// SPDX-License-Identifier: GPL-3.0-or-later
#include "stroke_model.h"

#include <QLineF>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <functional>

namespace QindaOffice::Ink {

namespace {
constexpr qreal kMinRenderWidth = 0.4;
}

QRectF Stroke::bounds() const
{
    if (samples.isEmpty()) {
        return {};
    }
    QRectF rect(samples.first().pos, QSizeF(1, 1));
    qreal maxWidth = width;
    for (const StrokeSample &sample : samples) {
        rect |= QRectF(sample.pos, QSizeF(1, 1));
        maxWidth = qMax(maxWidth, widthAt(int(&sample - samples.data())));
    }
    return rect.adjusted(-maxWidth, -maxWidth, maxWidth, maxWidth);
}

qreal Stroke::widthAt(int sample) const
{
    if (sample < 0 || sample >= samples.size()) {
        return width;
    }
    const qreal pressure = qBound(0.0, samples.at(sample).pressure, 1.0);
    return qMax(kMinRenderWidth, width * (0.5 + pressure));
}

Stroke &StrokeLayer::appendStroke(const Stroke &stroke)
{
    m_strokes.append(stroke);
    return m_strokes.last();
}

void StrokeLayer::updateLast(const Stroke &stroke)
{
    if (!m_strokes.isEmpty()) {
        m_strokes.last() = stroke;
    }
}

const Stroke *StrokeLayer::strokeAt(const QPointF &pos, qreal tolerance) const
{
    for (auto it = m_strokes.crbegin(); it != m_strokes.crend(); ++it) {
        const qreal reach = it->width / 2.0 + tolerance;
        for (const StrokeSample &sample : it->samples) {
            if (QLineF(sample.pos, pos).length() <= reach) {
                return &*it;
            }
        }
    }
    return nullptr;
}

bool StrokeLayer::removeAt(const QPointF &pos, qreal tolerance)
{
    for (auto it = m_strokes.begin(); it != m_strokes.end(); ++it) {
        const qreal reach = it->width / 2.0 + tolerance;
        for (const StrokeSample &sample : it->samples) {
            if (QLineF(sample.pos, pos).length() <= reach) {
                m_strokes.erase(it);
                return true;
            }
        }
    }
    return false;
}

void StrokeLayer::clear()
{
    m_strokes.clear();
}

void StrokeLayer::removeIndices(const QVector<int> &indices)
{
    QVector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (const int index : sorted) {
        if (index >= 0 && index < m_strokes.size()) {
            m_strokes.remove(index);
        }
    }
}

void StrokeLayer::replaceStroke(int index, const QVector<Stroke> &fragments)
{
    if (index < 0 || index >= m_strokes.size()) {
        return;
    }
    m_strokes.remove(index);
    for (int i = fragments.size() - 1; i >= 0; --i) {
        m_strokes.insert(index, fragments.at(i));
    }
}

QRectF StrokeLayer::bounds() const
{
    QRectF rect;
    for (const Stroke &stroke : m_strokes) {
        rect |= stroke.bounds();
    }
    return rect;
}

QByteArray StrokeLayer::toJson() const
{
    QJsonArray strokes;
    for (const Stroke &stroke : m_strokes) {
        QJsonArray samples;
        for (const StrokeSample &sample : stroke.samples) {
            samples.append(QJsonArray{sample.pos.x(), sample.pos.y(), sample.pressure,
                                      sample.tiltX, sample.tiltY});
        }
        strokes.append(QJsonObject{
            {QStringLiteral("samples"), samples},
            {QStringLiteral("tool"), int(stroke.tool)},
            {QStringLiteral("color"), stroke.color.name(QColor::HexArgb)},
            {QStringLiteral("width"), stroke.width},
        });
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("strokes"), strokes}}).toJson(QJsonDocument::Compact);
}

bool StrokeLayer::fromJson(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    m_strokes.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("strokes")).toArray()) {
        const QJsonObject object = value.toObject();
        Stroke stroke;
        stroke.tool = StrokeTool(object.value(QStringLiteral("tool")).toInt());
        stroke.color = QColor::fromString(object.value(QStringLiteral("color")).toString());
        stroke.width = object.value(QStringLiteral("width")).toDouble(2.5);
        for (const QJsonValue &sampleValue : object.value(QStringLiteral("samples")).toArray()) {
            const QJsonArray sample = sampleValue.toArray();
            if (sample.size() >= 3) {
                stroke.samples.append({QPointF(sample.at(0).toDouble(), sample.at(1).toDouble()),
                                       sample.at(2).toDouble(),
                                       sample.size() > 3 ? sample.at(3).toDouble() : 0.0,
                                       sample.size() > 4 ? sample.at(4).toDouble() : 0.0});
            }
        }
        stroke.complete = true;
        m_strokes.append(stroke);
    }
    return true;
}

} // namespace QindaOffice::Ink
