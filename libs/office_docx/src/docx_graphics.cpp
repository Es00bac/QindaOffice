// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "chart_interchange.h"
#include "drawing_interchange.h"
#include "slide_renderer.h"
#include "ooxml_namespaces.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTextDocument>
#include <QUuid>
#include <cmath>

namespace QindaOffice::Docx {
namespace {
using namespace Graphics;
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kC = QString::fromUtf16(Ooxml::Ns::C);
const QString kWPS = QString::fromUtf16(Ooxml::Ns::WPS);
QImage render(const QTextImageFormat &format)
{
    const QSizeF logical(format.width(), format.height());
    const QSize pixels(qBound(16, qRound(logical.width() * 2), 4096), qBound(16, qRound(logical.height() * 2), 4096));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(pixels.width() / logical.width(), pixels.height() / logical.height());
    const auto data = format.property(kGraphicData).toMap();
    const QString kind = format.stringProperty(kGraphicKind);
    if (kind == u"chart") {
        painter.fillRect(QRectF(QPointF(), logical), Qt::white);
        Show::ChartIO::paint(&painter, QRectF(QPointF(), logical), Show::ChartIO::fromMap(data));
    } else {
        QList<Show::Shape> shapes;
        if (kind == u"shape") {
            auto shape = Show::shapeFromJson(QJsonObject::fromVariantMap(data));
            shape.rect.setSize(logical * 0.75); shapes.append(shape);
        }
        else shapes = Show::shapesFromJson(data.value(QStringLiteral("render")).toByteArray());
        if (shapes.isEmpty() && kind == u"smartArt") {
            const auto texts = data.value(QStringLiteral("texts")).toStringList();
            for (int i = 0; i < texts.size(); ++i) {
                auto shape = defaultShape(); shape.rect = QRectF(0, i * 54, 240, 44); replaceText(shape, texts.at(i)); shapes.append(shape);
            }
        }
        QRectF bounds;
        for (const auto &shape : shapes) bounds = bounds.isNull() ? shape.rect : bounds.united(shape.rect);
        if (bounds.width() > 0 && bounds.height() > 0) {
            // Fit the cached drawing's whole coordinate space, preserving
            // aspect ratio. This same image resource is printed and cloned.
            const qreal scale = qMin((logical.width() - 4) / bounds.width(), (logical.height() - 4) / bounds.height());
            painter.translate((logical.width() - bounds.width() * scale) / 2, (logical.height() - bounds.height() * scale) / 2);
            painter.scale(scale, scale); painter.translate(-bounds.topLeft());
            for (const auto &shape : shapes) Show::renderShape(&painter, shape);
        }
    }
    return image;
}
} // namespace
void refreshGraphic(QTextImageFormat &format, QTextDocument &document)
{
    const QImage image = render(format);
    QByteArray identity = QJsonDocument(QJsonObject::fromVariantMap(format.property(kGraphicData).toMap())).toJson(QJsonDocument::Compact);
    identity += format.stringProperty(kGraphicKind).toUtf8() + QByteArray::number(format.width()) + '/' + QByteArray::number(format.height());
    const QString name = QStringLiteral("qwgraphic:") + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    document.addResource(QTextDocument::ImageResource, QUrl(name), image); format.setName(name);
}
bool readGraphic(const Ooxml::XmlElement &frame, const std::shared_ptr<const Ooxml::Package> &source,
                 const QString &story, const Ooxml::Theme &theme, QTextImageFormat &format, QImage &image)
{
    if (!std::isfinite(format.width()) || !std::isfinite(format.height()) || format.width() < 1 || format.height() < 1) return false;
    const auto *data = frame.descendant(kA, u"graphicData");
    if (!data) return false;
    QVariantMap value;
    QString kind, part;
    if (const auto *reference = data->child(kC, u"chart")) {
        part = related(source, story, reference->attribute(kR, u"id"));
        const auto chart = Show::ChartIO::read(source, part, theme);
        if (chart.isEmpty() || chart.type.isEmpty()) return false;
        for (const auto &plot : chart.plots) if (plot.type.isEmpty()) return false;
        kind = QStringLiteral("chart"); value = Show::ChartIO::toMap(chart);
    } else if (const auto *wsp = data->child(kWPS, u"wsp")) {
        auto shape = Show::DrawingIO::wordShape(*wsp, source, story, theme);
        if (shape.rect.isEmpty()) shape.rect = QRectF(0, 0, format.width() * 0.75, format.height() * 0.75);
        if (shape.geometry.isEmpty() && shape.customGeometry.isEmpty() && shape.displayText().isEmpty()) return false;
        kind = QStringLiteral("shape"); value = Show::shapeToJson(shape).toVariantMap();
    } else if (data->attr(u"uri").endsWith(u"/diagram")) {
        value = readDiagram(*data, source, story, theme);
        if (value.isEmpty()) return false;
        kind = QStringLiteral("smartArt"); part = value.value(QStringLiteral("dataPart")).toString();
    } else return false;
    format.setProperty(kGraphicKind, kind); format.setProperty(kGraphicData, value);
    format.setProperty(kGraphicSnapshot, value); format.setProperty(kGraphicSource, part);
    format.setProperty(kDrawingSize, QSizeF(format.width(), format.height()));
    image = render(format);
    return true;
}
QTextImageFormat newGraphic(const QString &kind, QTextDocument &document)
{
    QTextImageFormat format;
    if (kind != u"chart" && kind != u"shape" && kind != u"smartArt") return format;
    const QSizeF size = kind == u"shape" ? QSizeF(320, 160) : QSizeF(400, 260);
    auto run = drawingRun(kind, size);
    auto *data = descendant(*run, kA, u"graphicData");
    QVariantMap value;
    if (kind == u"chart") {
        Show::ChartData chart; chart.type = QStringLiteral("column"); chart.grouping = QStringLiteral("clustered");
        Show::ChartSeries series; series.name = QStringLiteral("Series 1");
        series.categories = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}; series.values = {4, 7, 5};
        chart.series = {series}; value = Show::ChartIO::toMap(chart);
        data->appendElement(kC, QStringLiteral("chart"));
    } else if (kind == u"shape") {
        const auto shape = defaultShape(); value = Show::shapeToJson(shape).toVariantMap(); data->appendElement(shapeXml(shape));
    } else {
        value = newDiagram(); data->appendElement(QString::fromUtf16(Ooxml::Ns::DGM), QStringLiteral("relIds"));
    }
    format.setWidth(size.width()); format.setHeight(size.height());
    format.setProperty(kGraphicKind, kind); format.setProperty(kGraphicData, value);
    format.setProperty(kInlineXml, Ooxml::serializeFragment(*run));
    format.setProperty(kInlineLabel, kind == u"smartArt" ? QStringLiteral("SmartArt graphic") : kind == u"chart" ? QStringLiteral("Chart") : QStringLiteral("Shape"));
    // UUID-derived serial plus a unique resource makes adjacent copied or
    // newly inserted objects separate preserved runs in the writer.
    format.setProperty(kInlineSerial, int(qHash(QUuid::createUuid()) & 0x3fffffff) + 1);
    refreshGraphic(format, document); return format;
}
} // namespace QindaOffice::Docx
