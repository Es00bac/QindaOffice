// SPDX-License-Identifier: GPL-3.0-or-later
#include "export.h"

#include "painter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QSaveFile>
#include <QSvgGenerator>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DiagramExport", text);
}

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

void paintRegion(QPainter *painter, const Page &page, const QSet<QString> &ids, const QRectF &region, bool transparent)
{
    painter->translate(-region.topLeft());
    if (!transparent) {
        painter->fillRect(region, page.background.isValid() && page.background.alpha() > 0 ? page.background
                                                                                            : QColor(Qt::white));
    }
    PaintOptions options;
    options.background = false; // the region fill above covers selection exports too
    options.only = ids;
    paintPage(painter, page, options);
}

// Paints one page scaled to fit `target` (device units), centred; a
// landscape page on a portrait sheet (or the reverse) is turned a quarter
// so it uses the whole sheet.
void paintFitted(QPainter *painter, const Page &page, const QRectF &target)
{
    const QSizeF size = page.size;
    const bool turn = (size.width() > size.height()) != (target.width() > target.height())
        && std::abs(size.width() - size.height()) > 1;
    const QSizeF laid = turn ? size.transposed() : size;
    const double scale = std::min(target.width() / laid.width(), target.height() / laid.height());
    painter->save();
    painter->translate(target.center());
    if (turn) {
        painter->rotate(-90);
    }
    painter->scale(scale, scale);
    painter->translate(-size.width() / 2, -size.height() / 2);
    painter->setClipRect(QRectF(QPointF(0, 0), size), Qt::IntersectClip);
    paintPage(painter, page, {});
    painter->restore();
}

} // namespace

QRectF exportRegion(const Page &page, const QSet<QString> &ids, double margin)
{
    if (ids.isEmpty()) {
        return QRectF(QPointF(0, 0), page.size);
    }
    const QRectF bounds = itemsBounds(page, ids);
    return bounds.isNull() ? QRectF(QPointF(0, 0), page.size) : bounds.adjusted(-margin, -margin, margin, margin);
}

QImage renderImage(const Page &page, const QSet<QString> &ids, double scale, bool transparent)
{
    const QRectF region = exportRegion(page, ids);
    const double s = std::clamp(scale, 0.05, 16.0);
    const QSize pixels(std::max(1, int(std::ceil(region.width() * s))), std::max(1, int(std::ceil(region.height() * s))));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // 96 dpi at scale 1: page units are CSS pixels.
    const int dotsPerMeter = int(std::round(96.0 * s / 0.0254));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(s, s);
    paintRegion(&painter, page, ids, region, transparent);
    painter.end();
    return image;
}

bool exportPng(const QString &path, const Page &page, const QSet<QString> &ids, double scale, bool transparent,
               QString *error)
{
    const QImage image = renderImage(page, ids, scale, transparent);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        return fail(error, tr("Could not write %1: %2").arg(path, file.errorString()));
    }
    return true;
}

QByteArray svgBytes(const Page &page, const QSet<QString> &ids)
{
    const QRectF region = exportRegion(page, ids);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QSvgGenerator generator;
    generator.setOutputDevice(&buffer);
    generator.setSize(region.size().toSize());
    generator.setViewBox(QRectF(QPointF(0, 0), region.size()));
    generator.setResolution(96);
    generator.setTitle(page.name);
    generator.setDescription(tr("Drawn with QindaDiagram"));
    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing);
    paintRegion(&painter, page, ids, region, false);
    painter.end();
    return buffer.data();
}

bool exportSvg(const QString &path, const Page &page, const QSet<QString> &ids, QString *error)
{
    const QByteArray bytes = svgBytes(page, ids);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return fail(error, tr("Could not write %1: %2").arg(path, file.errorString()));
    }
    return true;
}

PagesPainter pagesPainter(const Drawing &drawing, const QStringList &pageIds)
{
    QVector<Page> pages;
    for (const Page &page : drawing.pages) {
        if (pageIds.isEmpty() || pageIds.contains(page.id)) {
            pages.append(page);
        }
    }
    return [pages](QPagedPaintDevice *device, QPainter *painter) {
        for (int i = 0; i < pages.size(); ++i) {
            if (i > 0) {
                device->newPage();
            }
            // The printable area in device units: the painter's window is
            // the device's paint rectangle.
            const QRect paintRect = painter->viewport();
            paintFitted(painter, pages.at(i), QRectF(0, 0, paintRect.width(), paintRect.height()));
        }
    };
}

} // namespace QindaOffice::Diagram
