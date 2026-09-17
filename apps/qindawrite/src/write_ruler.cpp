// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_ruler.h"

#include "page_text_view.h"
#include "write_document.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

WriteRuler::WriteRuler(PageTextView *view, WriteDocument *document, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_document(document)
{
    setObjectName(QStringLiteral("writeRuler"));
    setFixedHeight(kHeight);
    setMouseTracking(true);
    // The ruler follows the view: its zoom, its horizontal scroll and the
    // cursor's paragraph all change what it should show.
    connect(m_view->control(), &QWidgetTextControl::cursorPositionChanged, this,
            &WriteRuler::refresh);
    connect(m_view, &PageTextView::zoomChanged, this, [this](qreal) { refresh(); });
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { refresh(); });
    connect(m_document, &WriteDocument::pageSetupChanged, this, &WriteRuler::refresh);
}

QSize WriteRuler::sizeHint() const
{
    return {200, kHeight};
}

QRectF WriteRuler::pageBand() const
{
    // The same rect the view paints the first page into, minus the vertical
    // part: the ruler must sit exactly above the paper, at the same zoom
    // and the same horizontal offset, or its ticks are a lie.
    const QRectF page = m_view->geometry()->pageRect(0, m_view->viewport()->width());
    return {page.left() - m_view->horizontalScrollBar()->value(), 0.0, page.width(),
            qreal(kHeight)};
}

qreal WriteRuler::documentXToView(qreal docX) const
{
    return pageBand().left() + docX * m_view->zoom();
}

WriteRuler::Markers WriteRuler::markers() const
{
    const QTextBlockFormat format = m_view->control()->textCursor().blockFormat();
    const qreal margin = m_document->pageMargin();
    const qreal pageWidth = m_document->pageSize().width();
    Markers out;
    out.left = margin + format.leftMargin();
    out.firstLine = out.left + format.textIndent();
    out.right = pageWidth - margin - format.rightMargin();
    return out;
}

QRectF WriteRuler::handleRect(Handle handle) const
{
    const Markers m = markers();
    qreal docX = 0;
    qreal top = 0;
    switch (handle) {
    case Handle::FirstLine:
        docX = m.firstLine;
        top = 1;
        break;
    case Handle::Left:
        docX = m.left;
        top = kHeight - kHandlePx - 1;
        break;
    case Handle::Right:
        docX = m.right;
        top = kHeight - kHandlePx - 1;
        break;
    case Handle::None:
        return {};
    }
    const qreal x = documentXToView(docX);
    return {x - kHandlePx / 2.0, top, kHandlePx, kHandlePx};
}

WriteRuler::Handle WriteRuler::handleAt(const QPointF &pos) const
{
    // First line sits on the top row, the two indents on the bottom row, so
    // the three never fight for the same pixels even when they line up.
    for (const Handle handle : {Handle::FirstLine, Handle::Left, Handle::Right}) {
        if (handleRect(handle).adjusted(-2, -2, 2, 2).contains(pos)) {
            return handle;
        }
    }
    return Handle::None;
}

void WriteRuler::refresh()
{
    update();
}

void WriteRuler::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window().color());
    const QRectF band = pageBand();
    if (band.width() <= 0) {
        return;
    }
    const qreal margin = m_document->pageMargin();
    const qreal zoom = m_view->zoom();

    // The text column is the light part; the margins are shaded, which is
    // what makes "where does my text start" readable at a glance.
    painter.fillRect(band, palette().mid().color());
    const QRectF column(documentXToView(margin), band.top(),
                        (m_document->pageSize().width() - 2 * margin) * zoom, band.height());
    painter.fillRect(column, palette().base().color());
    painter.setPen(palette().dark().color());
    painter.drawRect(band.adjusted(0, 0, -1, -1));

    // Centimetre ticks, numbered from the text column's left edge — a ruler
    // numbered from the paper edge makes every indent read one margin out.
    painter.setPen(palette().text().color());
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 2));
    painter.setFont(small);
    const qreal pageWidth = m_document->pageSize().width();
    for (int cm = 0; margin + cm * kPxPerCm <= pageWidth; ++cm) {
        const qreal x = documentXToView(margin + cm * kPxPerCm);
        if (x < band.left() - 1 || x > band.right() + 1) {
            continue;
        }
        painter.drawLine(QPointF(x, kHeight / 2.0 - 3), QPointF(x, kHeight / 2.0 + 3));
        if (cm > 0 && cm % 2 == 0) {
            painter.drawText(QRectF(x - 12, 1, 24, kHeight / 2.0 - 2),
                             Qt::AlignHCenter | Qt::AlignVCenter, QString::number(cm));
        }
    }

    painter.setPen(palette().highlight().color().darker(140));
    painter.setBrush(palette().highlight().color());
    for (const Handle handle : {Handle::FirstLine, Handle::Left, Handle::Right}) {
        const QRectF marker = handleRect(handle);
        if (marker.isNull()) {
            continue;
        }
        // First line points down, the indents point up: the shape says which
        // one the user has hold of.
        const QPolygonF triangle = handle == Handle::FirstLine
            ? QPolygonF({marker.topLeft(), marker.topRight(),
                         QPointF(marker.center().x(), marker.bottom())})
            : QPolygonF({marker.bottomLeft(), marker.bottomRight(),
                         QPointF(marker.center().x(), marker.top())});
        painter.drawPolygon(triangle);
    }
}

void WriteRuler::mousePressEvent(QMouseEvent *event)
{
    m_dragging = handleAt(event->position());
    if (m_dragging == Handle::None) {
        QWidget::mousePressEvent(event);
    }
}

void WriteRuler::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging != Handle::None) {
        applyDrag(event->position());
        return;
    }
    setCursor(handleAt(event->position()) == Handle::None ? Qt::ArrowCursor : Qt::SizeHorCursor);
    QWidget::mouseMoveEvent(event);
}

void WriteRuler::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = Handle::None;
    QWidget::mouseReleaseEvent(event);
}

void WriteRuler::applyDrag(const QPointF &pos)
{
    const qreal zoom = m_view->zoom();
    if (zoom <= 0) {
        return;
    }
    const qreal margin = m_document->pageMargin();
    const qreal pageWidth = m_document->pageSize().width();
    // View x back to document x, then to an indent relative to the margin.
    const qreal docX = qBound(margin, (pos.x() - pageBand().left()) / zoom, pageWidth - margin);
    QTextCursor cursor = m_view->control()->textCursor();
    QTextBlockFormat format = cursor.blockFormat();
    switch (m_dragging) {
    case Handle::Left: {
        const qreal left = docX - margin;
        // Dragging the left indent carries the first line with it, the way
        // every word processor does — otherwise the hanging indent changes
        // every time the paragraph moves.
        format.setLeftMargin(qMax(0.0, left));
        break;
    }
    case Handle::FirstLine:
        format.setTextIndent(docX - margin - format.leftMargin());
        break;
    case Handle::Right:
        format.setRightMargin(qMax(0.0, pageWidth - margin - docX));
        break;
    case Handle::None:
        return;
    }
    cursor.mergeBlockFormat(format);
    m_document->textDocument()->setModified(true);
    m_view->viewport()->update();
    update();
}

} // namespace QindaOffice::Apps::Write
