// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_view.h"

#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTabletEvent>
#include <QTextEdit>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Notes {

using namespace QindaOffice::Ink;

NotePageView::NotePageView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(int(m_pageWidth) + 48, int(m_minPageHeight) + 48);
    setAttribute(Qt::WA_AcceptTouchEvents, false); // pens only: palm baseline
}

QRectF NotePageView::pageRect() const
{
    const qreal height = qMax(m_minPageHeight * m_zoom, growHeight());
    const qreal pageW = m_pageWidth * m_zoom;
    return {(width() - pageW) / 2.0, 24.0, pageW, height};
}

void NotePageView::setPenWidth(qreal width)
{
    m_penWidth = qBound(1.0, width, 24.0);
}

void NotePageView::setPenColor(const QColor &color)
{
    if (color.isValid()) {
        m_penColor = color;
    }
}

void NotePageView::setZoom(qreal zoom)
{
    const qreal next = qBound(0.5, zoom, 3.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    updateGeometry();
    syncBlocksGeometry();
    update();
}

void NotePageView::setBackground(const QString &background)
{
    if (m_page == nullptr || m_page->envelope.background == background) {
        return;
    }
    m_page->envelope.background = background;
    update();
    emit pageEdited();
}

// Widget (event) coordinates -> page coordinates (strokes are stored in
// page space so zoom never rewrites the document).
QPointF NotePageView::toPagePos(const QPointF &widgetPos) const
{
    return (widgetPos - pageRect().topLeft()) / m_zoom;
}

qreal NotePageView::growHeight() const
{
    if (m_page == nullptr) {
        return m_minPageHeight;
    }
    qreal contentBottom = m_page->envelope.ink.bounds().bottom() + 80;
    for (const QTextEdit *block : m_textBlocks) {
        contentBottom = qMax(contentBottom, qreal(block->y() + block->height() + 40));
    }
    const int pages = qBound(1, int(std::ceil(contentBottom / m_minPageHeight)), int(kMaxPages));
    // The widget height of the page: page-space height times zoom.
    return pages * m_minPageHeight * m_zoom;
}

void NotePageView::setPage(NotePage *page)
{
    m_page = page;
    m_undoStack.clear();
    qDeleteAll(m_textBlocks);
    m_textBlocks.clear();
    if (m_page != nullptr) {
        rebuildGrid();
        syncBlocks();
    }
    updateGeometry();
    update();
}

void NotePageView::pushUndoSnapshot()
{
    m_undoStack.append(m_page->envelope.ink);
    if (m_undoStack.size() > 100) {
        m_undoStack.removeFirst();
    }
}

void NotePageView::undo()
{
    if (m_page == nullptr || m_undoStack.isEmpty()) {
        return;
    }
    m_page->envelope.ink = m_undoStack.takeLast();
    rebuildGrid();
    update();
    emit pageEdited();
}

void NotePageView::rebuildGrid()
{
    m_grid.rebuild(m_page != nullptr ? m_page->envelope.ink.strokes() : QVector<Stroke>{});
}

void NotePageView::syncBlocks()
{
    // Text blocks are stored in the envelope's blocksJson; v1 keeps one
    // QTextEdit per block, positioned under the previous content.
    const QJsonObject blocks = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    const QJsonArray list = blocks.value(QStringLiteral("blocks")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("text")) {
            continue;
        }
        auto *edit = new QTextEdit(this);
        edit->setPlainText(object.value(QStringLiteral("text")).toString());
        edit->setAcceptRichText(false);
        edit->show();
        m_textBlocks.append(edit);
    }
    syncBlocksGeometry();
}

void NotePageView::addTextBlock()
{
    if (m_page == nullptr) {
        return;
    }
    QJsonObject root = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                              {QStringLiteral("text"), QString()}});
    root.insert(QStringLiteral("blocks"), blocks);
    m_page->envelope.blocksJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    qDeleteAll(m_textBlocks);
    m_textBlocks.clear();
    syncBlocks();
    if (!m_textBlocks.isEmpty()) {
        m_textBlocks.last()->setFocus();
    }
    emit pageEdited();
    update();
}

void NotePageView::commitStroke()
{
    const Stroke stroke = m_builder.end();
    m_inking = false;
    if (stroke.samples.size() < 2) {
        update();
        return;
    }
    pushUndoSnapshot();
    if (m_tool == NoteTool::Eraser) {
        // Eraser strokes are not stored; they remove what they touched.
        for (const StrokeSample &sample : stroke.samples) {
            while (m_page->envelope.ink.removeAt(sample.pos, 6.0)) {
            }
        }
    } else {
        m_page->envelope.ink.appendStroke(stroke);
    }
    rebuildGrid();
    updateGeometry();
    update();
    emit pageEdited();
}

bool NotePageView::event(QEvent *event)
{
    if (m_page == nullptr) {
        return QWidget::event(event);
    }
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        // Palm rejection baseline: only pen input inks; touch/cursor events
        // (a resting palm registers as touch) never reach the stroke layer.
        if (tablet->pointerType() != QPointingDevice::PointerType::Pen) {
            return true; // consumed and ignored
        }
        const QPointF pos = toPagePos(tablet->position());
        const StrokeSample sample{pos, tablet->pressure(), tablet->xTilt(), tablet->yTilt()};
        if (event->type() == QEvent::TabletPress) {
            const bool highlight = m_tool == NoteTool::Highlighter;
            m_builder.begin(sample,
                            m_tool == NoteTool::Highlighter ? StrokeTool::Highlighter
                                                            : StrokeTool::Pen,
                            highlight ? QColor(255, 210, 0) : m_penColor,
                            highlight ? 6.0 : m_penWidth);
            m_inking = true;
        } else if (event->type() == QEvent::TabletMove && m_inking) {
            m_builder.add(sample);
            m_page->envelope.ink.updateLast(m_builder.current());
            update();
        } else if (event->type() == QEvent::TabletRelease && m_inking) {
            commitStroke();
        }
        return true;
    }
    // Mouse fallback (no tablet attached): left-drag inks with fixed pressure.
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
        && pageRect().contains(static_cast<QMouseEvent *>(event)->position())
        && m_tool != NoteTool::Text) {
        const bool highlight = m_tool == NoteTool::Highlighter;
        m_builder.begin({toPagePos(static_cast<QMouseEvent *>(event)->position()), 0.6},
                        highlight ? StrokeTool::Highlighter : StrokeTool::Pen,
                        highlight ? QColor(255, 210, 0) : m_penColor,
                        highlight ? 6.0 : m_penWidth);
        m_inking = true;
        return true;
    }
    if (event->type() == QEvent::MouseMove && m_inking) {
        m_builder.add({toPagePos(static_cast<QMouseEvent *>(event)->position()), 0.6});
        m_page->envelope.ink.updateLast(m_builder.current());
        update();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && m_inking) {
        commitStroke();
        return true;
    }
    return QWidget::event(event);
}

void NotePageView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window().color());
    const QRectF page = pageRect();
    painter.fillRect(page, palette().base().color());
    painter.setPen(palette().dark().color());
    painter.drawRect(page.adjusted(0, 0, -1, -1));

    // Backgrounds (plan 6.4).
    const QString background = m_page != nullptr ? m_page->envelope.background
                                                 : QStringLiteral("blank");
    painter.setPen(QPen(palette().mid().color(), 1));
    if (background == QLatin1String("ruled")) {
        for (qreal y = page.top() + 32; y < page.bottom(); y += 28) {
            painter.drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
    } else if (background == QLatin1String("grid") || background == QLatin1String("dotted")) {
        const qreal step = background == QLatin1String("grid") ? 28 : 22;
        for (qreal y = page.top() + 24; y < page.bottom(); y += step) {
            for (qreal x = page.left() + 24; x < page.right(); x += step) {
                if (background == QLatin1String("grid")) {
                    painter.drawPoint(QPointF(x, y));
                    painter.drawLine(QPointF(x, y), QPointF(qMin(x + step, page.right() - 24), y));
                } else {
                    painter.drawPoint(QPointF(x, y));
                }
            }
        }
    }

    if (m_page != nullptr) {
        painter.setClipRect(page);
        // The spatial grid indexes page coordinates; query in page space
        // and render strokes through the zoom transform.
        const QRect pageClip = QRectF(event->rect().x() / m_zoom, event->rect().y() / m_zoom,
                                      event->rect().width() / m_zoom,
                                      event->rect().height() / m_zoom)
                                   .toAlignedRect();
        const QVector<int> visible = m_grid.query(pageClip).values();
        painter.save();
        painter.translate(page.topLeft());
        painter.scale(m_zoom, m_zoom);
        for (const int index : visible) {
            renderStroke(&painter, m_page->envelope.ink.strokes().at(index));
        }
        if (m_inking) {
            renderStroke(&painter, m_builder.current());
        }
        painter.restore();
        painter.setClipping(false);
    }
}

void NotePageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncBlocksGeometry();
}

void NotePageView::syncBlocksGeometry()
{
    // Positions derive from the block index in PAGE coordinates, so zoom
    // repositions every block consistently (fonts intentionally stay
    // unscaled - the text tool is for typed notes, not slides).
    const QRectF page = pageRect();
    int index = 0;
    for (QTextEdit *block : m_textBlocks) {
        const qreal yPage = 40 + index * 100;
        block->move(int(page.x() + 40 * m_zoom), int(page.y() + (yPage - 24) * m_zoom));
        block->resize(int(page.width() - 80 * m_zoom), int(90 * m_zoom));
        ++index;
    }
}

} // namespace QindaOffice::Apps::Notes
