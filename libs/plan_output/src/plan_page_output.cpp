// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_page_output.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>

namespace QindaOffice::Plan {

namespace {

// Scales the painter to points and returns the content rectangle: the
// device's paint rect inset until the paper edge is kPageMargin away on
// every side (a printer's own margins count toward it).
QRectF beginPage(QPagedPaintDevice *device, QPainter *painter)
{
    painter->resetTransform();
    painter->scale(device->logicalDpiX() / 72.0, device->logicalDpiY() / 72.0);
    const QPageLayout layout = device->pageLayout();
    const QRectF full = layout.fullRectPoints();
    const QRectF paint = layout.paintRectPoints();
    const qreal left = qMax<qreal>(0, kPageMargin - paint.left());
    const qreal top = qMax<qreal>(0, kPageMargin - paint.top());
    const qreal right = qMax<qreal>(0, kPageMargin - (full.right() - paint.right()));
    const qreal bottom = qMax<qreal>(0, kPageMargin - (full.bottom() - paint.bottom()));
    return QRectF(left, top, paint.width() - left - right, paint.height() - top - bottom);
}

} // namespace

QRectF contentArea(const QSizeF &pagePoints)
{
    return QRectF(kPageMargin, kPageMargin, pagePoints.width() - 2 * kPageMargin, pagePoints.height() - 2 * kPageMargin);
}

PagePainter pagedPainter(PageLayout layout)
{
    return [layout = std::move(layout)](QPagedPaintDevice *device, QPainter *painter) {
        const QRectF area = beginPage(device, painter);
        const PageSet set = layout(area);
        for (int page = 0; page < set.count; ++page) {
            if (page > 0) {
                device->newPage();
                beginPage(device, painter);
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            set.paint(painter, page);
            painter->restore();
        }
    };
}

QList<QImage> rasterPages(const PageLayout &layout, const QSizeF &pagePoints, qreal dpi, int maxPages)
{
    QList<QImage> out;
    const PageSet set = layout(contentArea(pagePoints));
    const QSize pixels((pagePoints * dpi / 72.0).toSize());
    for (int page = 0; page < set.count && (maxPages <= 0 || page < maxPages); ++page) {
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(PlanPaperStyle().paper);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.scale(dpi / 72.0, dpi / 72.0);
        set.paint(&painter, page);
        painter.end();
        out.append(image);
    }
    return out;
}

QPageSize defaultPageSize(const QLocale &locale)
{
    switch (locale.territory()) {
    case QLocale::UnitedStates:
    case QLocale::Canada:
    case QLocale::Mexico:
    case QLocale::Philippines:
    case QLocale::Chile:
    case QLocale::Colombia:
    case QLocale::Venezuela:
    case QLocale::CostaRica:
    case QLocale::Guatemala:
    case QLocale::PuertoRico:
        return QPageSize(QPageSize::Letter);
    default:
        return QPageSize(QPageSize::A4);
    }
}

bool writePdf(const QString &path, const PagePainter &painter, const QPageSize &pageSize,
              QPageLayout::Orientation orientation, QString *error)
{
    {
        QPdfWriter writer(path);
        writer.setCreator(QStringLiteral("QindaPlan"));
        writer.setPageLayout(QPageLayout(pageSize, orientation, QMarginsF(0, 0, 0, 0)));
        QPainter qp;
        if (!qp.begin(&writer)) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot write %1").arg(path);
            }
            return false;
        }
        painter(&writer, &qp);
        qp.end();
    }
    if (QFileInfo(path).size() <= 0) {
        if (error != nullptr) {
            *error = QStringLiteral("nothing was written to %1").arg(path);
        }
        return false;
    }
    return true;
}

QColor PlanPaperStyle::tone(const QString &name) const
{
    if (name == QLatin1String("danger")) {
        return critical;
    }
    if (name == QLatin1String("warning")) {
        return warning;
    }
    if (name == QLatin1String("good")) {
        return good;
    }
    return ink;
}

QColor PlanPaperStyle::tint(const QColor &c, qreal amount) const
{
    const qreal k = qBound<qreal>(0, amount, 1);
    return QColor::fromRgbF(float(c.redF() * k + paper.redF() * (1 - k)), float(c.greenF() * k + paper.greenF() * (1 - k)),
                            float(c.blueF() * k + paper.blueF() * (1 - k)));
}

QFont paperFont(int px, QFont::Weight weight)
{
    QFont f;
    // AGENT-GUARD (from books_io): no OpenType features and ligatures off,
    // so the PDF's text layer extracts exactly what is printed.
    f.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Liberation Sans")});
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(px);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setKerning(true);
    f.setFeature(QFont::Tag("liga"), 0);
    return f;
}

qreal textWidth(const QFont &font, const QString &text)
{
    return QFontMetricsF(font).horizontalAdvance(text);
}

QString elideText(const QFont &font, const QString &text, qreal width)
{
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, qMax<qreal>(0, width));
}

void drawTextAt(QPainter *painter, qreal x, qreal top, const QString &text, const QFont &font, const QColor &color,
                bool alignRight)
{
    painter->setFont(font);
    painter->setPen(color);
    const QFontMetricsF fm(font);
    const qreal w = fm.horizontalAdvance(text);
    painter->drawText(QPointF(alignRight ? x - w : x, top + fm.ascent()), text);
}

} // namespace QindaOffice::Plan
