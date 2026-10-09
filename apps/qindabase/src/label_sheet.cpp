// SPDX-License-Identifier: GPL-3.0-or-later
// LabelSheet and LabelPageItem: mailing labels painted once for screen,
// printer and PDF. See label_sheet.h for the contract.
#include "label_sheet.h"

#include <QFontMetricsF>
#include <QPageSize>
#include <QPainter>
#include <QPaintDevice>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

constexpr qreal kInsetPoints = 9;   // 1/8 in inside each label's edge
constexpr qreal kMinimumFont = 6;   // points
// AGENT-NOTE (D-004): the paper's own colours, the one allowed literal.
const QColor kPaper(255, 255, 255);
const QColor kInk(0, 0, 0);

QFont labelFont(QPainter *painter, qreal points, qreal pixelsPerPoint)
{
    QFont font(QStringLiteral("sans-serif"));
    // Size in the painter's device: `points` on paper at pixelsPerPoint.
    const qreal dpi = painter->device() != nullptr ? painter->device()->logicalDpiY() : 96.0;
    font.setPointSizeF(points * pixelsPerPoint * 72.0 / dpi);
    return font;
}

} // namespace

LabelSheet::LabelSheet(QObject *parent)
    : QObject(parent)
{
}

void LabelSheet::setRun(const QString &name, const Engine::LabelRun &run, qreal fontSize)
{
    m_name = name;
    m_run = run;
    m_fontSize = fontSize;
    emit changed();
}

void LabelSheet::clear()
{
    m_name.clear();
    m_run = Engine::LabelRun();
    emit changed();
}

QString LabelSheet::productLabel() const
{
    return m_run.product.product.isEmpty() ? QString() : m_run.product.toMap().value(QStringLiteral("label")).toString();
}

QPageLayout LabelSheet::pageLayout() const
{
    const QSizeF page = m_run.product.page.isEmpty() ? QSizeF(612, 792) : m_run.product.page;
    QPageLayout layout(QPageSize(page, QPageSize::Point), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
    layout.setMode(QPageLayout::FullPageMode);
    return layout;
}

void LabelSheet::paintPage(QPainter *painter, int index, qreal pixelsPerPoint) const
{
    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setPen(kInk);
    for (const Engine::LabelPlacement &label : m_run.labels) {
        if (label.page != index || label.lines.isEmpty()) {
            continue;
        }
        const QRectF box(label.rect.x() * pixelsPerPoint, label.rect.y() * pixelsPerPoint,
                         label.rect.width() * pixelsPerPoint, label.rect.height() * pixelsPerPoint);
        const QRectF text = box.adjusted(kInsetPoints * pixelsPerPoint, kInsetPoints / 2 * pixelsPerPoint,
                                         -kInsetPoints * pixelsPerPoint, -kInsetPoints / 2 * pixelsPerPoint);
        // Shrink to fit the label's height, then elide what is still too wide.
        qreal size = m_fontSize;
        QFont font = labelFont(painter, size, pixelsPerPoint);
        while (size > kMinimumFont
               && QFontMetricsF(font, painter->device()).lineSpacing() * label.lines.size() > text.height()) {
            size -= 0.5;
            font = labelFont(painter, size, pixelsPerPoint);
        }
        painter->setFont(font);
        const QFontMetricsF metrics(font, painter->device());
        painter->setClipRect(box);
        qreal y = text.top();
        for (const QString &line : label.lines) {
            if (y + metrics.height() > text.bottom() + metrics.descent()) {
                break;
            }
            painter->drawText(QPointF(text.left(), y + metrics.ascent()),
                              metrics.elidedText(line, Qt::ElideRight, text.width()));
            y += metrics.lineSpacing();
        }
        painter->setClipping(false);
    }
    painter->restore();
}

QVariantList LabelSheet::pageLabels(int index) const
{
    QVariantList out;
    for (const Engine::LabelPlacement &label : m_run.labels) {
        if (label.page == index) {
            out.append(QVariantMap{{QStringLiteral("x"), label.rect.x()},
                                   {QStringLiteral("y"), label.rect.y()},
                                   {QStringLiteral("width"), label.rect.width()},
                                   {QStringLiteral("height"), label.rect.height()},
                                   {QStringLiteral("lines"), label.lines}});
        }
    }
    return out;
}

LabelPageItem::LabelPageItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void LabelPageItem::setSheet(LabelSheet *sheet)
{
    if (m_sheet == sheet) {
        return;
    }
    if (m_sheet != nullptr) {
        disconnect(m_sheet, nullptr, this, nullptr);
    }
    m_sheet = sheet;
    if (m_sheet != nullptr) {
        connect(m_sheet, &LabelSheet::changed, this, [this] {
            updateSize();
            update();
        });
    }
    updateSize();
    update();
    emit sheetChanged();
}

void LabelPageItem::setPage(int page)
{
    if (m_page != page) {
        m_page = page;
        update();
        emit pageChanged();
    }
}

void LabelPageItem::setZoom(qreal zoom)
{
    zoom = qBound(0.1, zoom, 8.0);
    if (!qFuzzyCompare(m_zoom, zoom)) {
        m_zoom = zoom;
        updateSize();
        update();
        emit zoomChanged();
    }
}

void LabelPageItem::updateSize()
{
    const QSizeF points = m_sheet != nullptr ? m_sheet->pageSize() : QSizeF();
    setImplicitSize(points.width() / 72.0 * 96.0 * m_zoom, points.height() / 72.0 * 96.0 * m_zoom);
}

void LabelPageItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), kPaper);
    if (m_sheet == nullptr || m_sheet->pageSize().isEmpty() || width() <= 0) {
        return;
    }
    m_sheet->paintPage(painter, m_page, width() / m_sheet->pageSize().width());
}

} // namespace QindaOffice::Apps::Base
