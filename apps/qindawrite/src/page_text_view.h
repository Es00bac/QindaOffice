// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PageTextView (plan section 7.1): the QindaWrite editor surface. A
// QAbstractScrollArea hosts a private QWidgetTextControl (WidgetsPrivate,
// probe #5) for editing; the view owns geometry and painting only, so
// pagination is screen-true and the PDF exporter reuses the same page
// model. Fallback documented in the plan (QTextEdit continuous + print
// time pagination) was not needed: probe #5 verified the control path.

#include "page_geometry.h"

#include <QAbstractScrollArea>

class QWidgetTextControl;

namespace QindaOffice::Apps::Write {

class WriteDocument;

class PageTextView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit PageTextView(WriteDocument *document, QWidget *parent = nullptr);
    ~PageTextView() override;

    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] QWidgetTextControl *control() const { return m_control; }

    [[nodiscard]] qreal zoom() const { return m_geometry.zoom(); }
    void setZoom(qreal zoom);
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] PageGeometry *geometry() { return &m_geometry; }

signals:
    void zoomChanged(qreal zoom);
    void pageCountChanged(int pages);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery property) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateScrollBars();
    void sendMouse(QMouseEvent *event);
    [[nodiscard]] QPointF offsetFor(const QPointF &viewPos) const;
    void paintPage(QPainter &painter, int index, const QRect &clip);

    WriteDocument *m_document;
    QWidgetTextControl *m_control;
    PageGeometry m_geometry;
};

} // namespace QindaOffice::Apps::Write
