// SPDX-License-Identifier: GPL-3.0-or-later
#include "export.h"

#include "layers.h"
#include "painter.h"
#include "print_tiles.h"

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

void paintRegion(QPainter *painter, const Page &page, const QSet<QString> &ids, const QRectF &region, bool transparent,
                 const QVector<const Page *> &backgrounds)
{
    painter->translate(-region.topLeft());
    if (!transparent) {
        painter->fillRect(region, page.background.isValid() && page.background.alpha() > 0 ? page.background
                                                                                            : QColor(Qt::white));
    }
    PaintOptions options;
    options.background = false; // the region fill above covers selection exports too
    options.only = ids;
    options.printing = true;     // exports follow the paper rules (M87)
    options.backgrounds = backgrounds;
    paintPage(painter, page, options);
}

// Paints one sheet of a page (print_tiles.h): the tile's part of the page
// at its scale, or — a page fitted on one sheet that is turned — the whole
// page a quarter turn round the sheet's centre.
void paintTile(QPainter *painter, const Page &page, const QVector<const Page *> &backgrounds, const PrintTile &tile,
               QSizeF sheet)
{
    PaintOptions options;
    options.printing = true;
    options.backgrounds = backgrounds;
    painter->save();
    if (tile.turned) {
        painter->translate(sheet.width() / 2, sheet.height() / 2);
        painter->rotate(-90);
        painter->scale(tile.scale, tile.scale);
        painter->translate(-page.size.width() / 2, -page.size.height() / 2);
        painter->setClipRect(QRectF(QPointF(0, 0), page.size), Qt::IntersectClip);
    } else {
        painter->translate(tile.offset);
        painter->scale(tile.scale, tile.scale);
        painter->translate(-tile.region.topLeft());
        painter->setClipRect(tile.region.intersected(QRectF(QPointF(0, 0), page.size)), Qt::IntersectClip);
    }
    paintPage(painter, page, options);
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

QImage renderImage(const Page &page, const QSet<QString> &ids, double scale, bool transparent,
                   const QVector<const Page *> &backgrounds)
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
    paintRegion(&painter, page, ids, region, transparent, backgrounds);
    painter.end();
    return image;
}

bool exportPng(const QString &path, const Page &page, const QSet<QString> &ids, double scale, bool transparent,
               QString *error, const QVector<const Page *> &backgrounds)
{
    const QImage image = renderImage(page, ids, scale, transparent, backgrounds);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        return fail(error, tr("Could not write %1: %2").arg(path, file.errorString()));
    }
    return true;
}

QByteArray svgBytes(const Page &page, const QSet<QString> &ids, const QVector<const Page *> &backgrounds)
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
    paintRegion(&painter, page, ids, region, false, backgrounds);
    painter.end();
    return buffer.data();
}

bool exportSvg(const QString &path, const Page &page, const QSet<QString> &ids, QString *error,
               const QVector<const Page *> &backgrounds)
{
    const QByteArray bytes = svgBytes(page, ids, backgrounds);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return fail(error, tr("Could not write %1: %2").arg(path, file.errorString()));
    }
    return true;
}

PagesPainter pagesPainter(const Drawing &drawing, const QStringList &pageIds)
{
    // Copies: the job paints the drawing as it was when it was made.
    struct Job {
        Page page;
        QVector<Page> backgrounds;
    };
    QVector<Job> jobs;
    for (const Page &page : drawing.pages) {
        // A background page prints under its pages, not on its own (Visio
        // prints the foreground pages) — unless it is asked for by name.
        if (pageIds.isEmpty() ? !page.backgroundPage : pageIds.contains(page.id)) {
            Job job{page, {}};
            for (const Page *back : backgroundChain(drawing, page)) {
                job.backgrounds.append(*back);
            }
            jobs.append(job);
        }
    }
    return [jobs](QPagedPaintDevice *device, QPainter *painter) {
        bool first = true;
        for (const Job &job : jobs) {
            QVector<const Page *> backgrounds;
            for (const Page &back : job.backgrounds) {
                backgrounds.append(&back);
            }
            // The printable area in device units: the painter's window is
            // the device's paint rectangle.
            const QRect paintRect = painter->viewport();
            const QSizeF sheet(paintRect.width(), paintRect.height());
            for (const PrintTile &tile : printTiles(job.page, sheet, device->logicalDpiX())) {
                if (!first) {
                    device->newPage();
                }
                first = false;
                paintTile(painter, job.page, backgrounds, tile, sheet);
            }
        }
    };
}

} // namespace QindaOffice::Diagram
