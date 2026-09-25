// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_output.h"

#include "paint_util.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>

namespace QindaOffice::Books {

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
        writer.setCreator(QStringLiteral("QindaBooks"));
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

namespace Paint {

QRectF beginPage(QPagedPaintDevice *device, QPainter *painter)
{
    painter->resetTransform();
    painter->scale(device->logicalDpiX() / 72.0, device->logicalDpiY() / 72.0);
    const QPageLayout layout = device->pageLayout();
    const QRectF full = layout.fullRectPoints();
    const QRectF paint = layout.paintRectPoints();
    // Painter origin is the device's paint-rect corner; inset from there
    // until the paper edge is at least kMinMargin away on every side.
    const qreal left = qMax<qreal>(0, kMinMargin - paint.left());
    const qreal top = qMax<qreal>(0, kMinMargin - paint.top());
    const qreal right = qMax<qreal>(0, kMinMargin - (full.right() - paint.right()));
    const qreal bottom = qMax<qreal>(0, kMinMargin - (full.bottom() - paint.bottom()));
    return QRectF(left, top, paint.width() - left - right, paint.height() - top - bottom);
}

QFont font(int px, QFont::Weight weight)
{
    QFont f;
    // AGENT-GUARD: no OpenType features here (tnum, calt …). Qt's PDF
    // engine writes a ToUnicode map only for a glyph's default mapping, so
    // alternate glyphs (Inter's tabular digits, its case-sensitive
    // parentheses and hyphens) print fine but vanish from the PDF's text:
    // search, copy and screen readers lose every amount. Noto Sans' default
    // figures are already tabular. tst_books_io_forms reads the text back.
    f.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Liberation Sans")});
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(px);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setKerning(true);
    // Ligatures off (this only ever selects the DEFAULT glyphs): "Coffee"
    // must extract as c-o-f-f-e-e, not "Coﬀee", or a search for the
    // customer's name in the PDF fails.
    f.setFeature(QFont::Tag("liga"), 0);
    return f;
}

qreal textWidth(const QFont &font, const QString &text)
{
    return QFontMetricsF(font).horizontalAdvance(text);
}

qreal lineHeight(const QFont &font)
{
    return QFontMetricsF(font).height();
}

qreal wrappedHeight(const QFont &font, const QString &text, qreal width)
{
    if (text.isEmpty()) {
        return lineHeight(font);
    }
    return QFontMetricsF(font).boundingRect(QRectF(0, 0, width, 1e6), Qt::TextWordWrap, text).height();
}

QString elide(const QFont &font, const QString &text, qreal width)
{
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

void drawText(QPainter *painter, qreal x, qreal top, const QString &text, const QFont &font, const QColor &color,
              bool alignRight)
{
    painter->setFont(font);
    painter->setPen(color);
    const QFontMetricsF fm(font);
    const qreal w = fm.horizontalAdvance(text);
    painter->drawText(QPointF(alignRight ? x - w : x, top + fm.ascent()), text);
}

} // namespace Paint
} // namespace QindaOffice::Books
