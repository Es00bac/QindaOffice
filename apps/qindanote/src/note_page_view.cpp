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
    // Text block geometry is in WIDGET space (offset by the page origin,
    // scaled by zoom); convert to page space before comparing against the
    // page-space minimum height. AGENT-GUARD: do NOT call pageRect() here —
    // pageRect() calls growHeight(); use the fixed 24px top offset instead.
    for (const QTextEdit *block : m_textBlocks) {
        const qreal blockBottomPage = (block->y() - 24.0) / m_zoom + block->height() / m_zoom;
        contentBottom = qMax(contentBottom, blockBottomPage + 40);
    }
    const int pages = qBound(1, int(std::ceil(contentBottom / m_minPageHeight)), int(kMaxPages));
    // The widget height of the page: page-space height times zoom.
    return pages * m_minPageHeight * m_zoom;
}

void NotePageView::setPage(NotePage *page)
{
    m_page = page;
    m_undoStack.clear();
    m_redoStack.clear();
    notifyHistoryAvailability();
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
    if (m_undoStack.size() > kMaxHistory) {
        m_undoStack.removeFirst();
    }
    // A new edit ends the old redo branch (standard editor semantics).
    m_redoStack.clear();
    notifyHistoryAvailability();
}

void NotePageView::notifyHistoryAvailability()
{
    emit undoAvailable(m_page != nullptr && !m_undoStack.isEmpty());
    emit redoAvailable(m_page != nullptr && !m_redoStack.isEmpty());
}

void NotePageView::undo()
{
    if (m_page == nullptr || m_undoStack.isEmpty()) {
        return;
    }
    // The state being left behind is what redo must come back to.
    m_redoStack.append(m_page->envelope.ink);
    if (m_redoStack.size() > kMaxHistory) {
        m_redoStack.removeFirst();
    }
    m_page->envelope.ink = m_undoStack.takeLast();
    notifyHistoryAvailability();
    rebuildGrid();
    update();
    emit pageEdited();
}

void NotePageView::redo()
{
    if (m_page == nullptr || m_redoStack.isEmpty()) {
        return;
    }
    m_undoStack.append(m_page->envelope.ink);
    if (m_undoStack.size() > kMaxHistory) {
        m_undoStack.removeFirst();
    }
    m_page->envelope.ink = m_redoStack.takeLast();
    notifyHistoryAvailability();
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
    // Text blocks are stored in the envelope's blocksJson; one QTextEdit per
    // block, positioned under the previous content.
    const QJsonObject blocks = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    const QJsonArray list = blocks.value(QStringLiteral("blocks")).toArray();
    m_syncingBlocks = true;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("text")) {
            continue;
        }
        auto *edit = new QTextEdit(this);
        edit->setPlainText(object.value(QStringLiteral("text")).toString());
        edit->setAcceptRichText(false);
        edit->show();
        // AGENT-GUARD: every editor writes its content back to blocksJson on
        // change — typed text must be persisted and must mark the page dirty.
        // (The v1 code only ever read the JSON, so typed text was lost on the
        // first addTextBlock/setPage/save.)
        connect(edit, &QTextEdit::textChanged, this, [this] { persistTextBlocks(); });
        m_textBlocks.append(edit);
    }
    m_syncingBlocks = false;
    syncBlocksGeometry();
}

void NotePageView::persistTextBlocks()
{
    if (m_page == nullptr || m_syncingBlocks) {
        return;
    }
    QJsonObject root = QJsonDocument::fromJson(m_page->envelope.blocksJson).object();
    QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    // Write the live editor contents back over the text entries in order.
    int editorIndex = 0;
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject object = blocks.at(i).toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("text")) {
            continue;
        }
        if (editorIndex >= m_textBlocks.size()) {
            break;
        }
        object.insert(QStringLiteral("text"), m_textBlocks.at(editorIndex)->toPlainText());
        blocks.replace(i, object);
        ++editorIndex;
    }
    root.insert(QStringLiteral("blocks"), blocks);
    m_page->envelope.blocksJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    emit pageEdited();
    update();
}

void NotePageView::addTextBlock()
{
    if (m_page == nullptr) {
        return;
    }
    // Persist the existing editors first: they are about to be destroyed and
    // recreated from the JSON.
    persistTextBlocks();
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
    Stroke stroke = m_builder.end();
    m_inking = false;
    if (stroke.samples.isEmpty()) {
        update();
        return;
    }
    if (stroke.samples.size() == 1) {
        // A pen tap (dot) makes no movement; synthesize a tiny second sample
        // so the stroke survives the renderer's and persistence's two-sample
        // minimum instead of silently vanishing.
        const StrokeSample only = stroke.samples.first();
        const qreal jitter = qMax(0.5, stroke.width / 6.0);
        stroke.samples.append({only.pos + QPointF(jitter, jitter), only.pressure, only.tiltX,
                               only.tiltY});
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
        // The stylus eraser end is a pen-class device: flip to the eraser
        // tool instead of consuming it as palm.
        if (tablet->pointerType() == QPointingDevice::PointerType::Eraser) {
            if (event->type() == QEvent::TabletPress) {
                m_toolBeforeEraser = m_tool;
                m_tool = NoteTool::Eraser;
            }
        } else if (tablet->pointerType() != QPointingDevice::PointerType::Pen) {
            return true; // consumed and ignored
        }
        const QPointF pos = toPagePos(tablet->position());
        const StrokeSample sample{pos, tablet->pressure(), tablet->xTilt(), tablet->yTilt()};
        if (event->type() == QEvent::TabletPress) {
            // AGENT-GUARD: the Text tool never inks — symmetric with the
            // mouse fallback below (the tablet branch missed this and drew
            // pen strokes while Text was selected). Strokes must also start
            // inside the page; ink in the margin is invisible but persisted.
            if (m_tool == NoteTool::Text || !pageRect().contains(tablet->position())) {
                if (tablet->pointerType() == QPointingDevice::PointerType::Eraser) {
                    m_tool = m_toolBeforeEraser;
                }
                return true;
            }
            const bool highlight = m_tool == NoteTool::Highlighter;
            m_builder.begin(sample,
                            m_tool == NoteTool::Highlighter ? StrokeTool::Highlighter
                                                            : StrokeTool::Pen,
                            highlight ? QColor(255, 210, 0) : m_penColor,
                            highlight ? 6.0 : m_penWidth);
            m_inking = true;
        } else if (event->type() == QEvent::TabletMove && m_inking) {
            // AGENT-GUARD: the in-progress stroke is rendered from
            // m_builder.current() in paintEvent and must NEVER touch the
            // committed layer during moves — an updateLast() here replaced
            // the previous real stroke, destroying it on the first move of
            // the next stroke.
            m_builder.add(sample);
            update();
        } else if (event->type() == QEvent::TabletRelease && m_inking) {
            commitStroke();
        }
        if (tablet->pointerType() == QPointingDevice::PointerType::Eraser
            && event->type() == QEvent::TabletRelease) {
            m_tool = m_toolBeforeEraser;
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
        // Same guard as the tablet path: preview stays out of the layer.
        m_builder.add({toPagePos(static_cast<QMouseEvent *>(event)->position()), 0.6});
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
    } else if (background == QLatin1String("grid")) {
        // A real grid: horizontal AND vertical lines through each node
        // (the earlier release drew only horizontals + points).
        for (qreal y = page.top() + 24; y < page.bottom(); y += 28) {
            painter.drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
        for (qreal x = page.left() + 24; x < page.right(); x += 28) {
            painter.drawLine(QPointF(x, page.top() + 24), QPointF(x, page.bottom() - 24));
        }
    } else if (background == QLatin1String("dotted")) {
        for (qreal y = page.top() + 24; y < page.bottom(); y += 22) {
            for (qreal x = page.left() + 24; x < page.right(); x += 22) {
                painter.drawPoint(QPointF(x, y));
            }
        }
    }

    if (m_page != nullptr) {
        painter.setClipRect(page);
        // The spatial grid indexes page coordinates; query in page space
        // and render strokes through the zoom transform. AGENT-GUARD: the
        // widget→page transform subtracts the page origin BEFORE dividing
        // by zoom (the earlier release skipped the subtraction, offsetting
        // the query rect and leaving strokes unpainted at zoom ≠ 100 %).
        const QRectF widgetClip = event->rect();
        const QRectF pageClip = QRectF((widgetClip.topLeft() - page.topLeft()) / m_zoom,
                                       widgetClip.size() / m_zoom);
        const QVector<int> visible = m_grid.query(pageClip.toAlignedRect()).values();
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
