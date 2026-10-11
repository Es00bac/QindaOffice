// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "equation_session.h"
#include "math_layout.h"
#include <QQuickPaintedItem>
namespace QindaOffice::Quick {
// Professional equation draft, using the same layout as document/print.
class EquationCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Quick::EquationSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selectionColor NOTIFY selectionColorChanged)
public:
    explicit EquationCanvas(QQuickItem *parent = nullptr);
    EquationSession *session() const { return m_session; }
    void setSession(EquationSession *session);
    void paint(QPainter *painter) override;
signals:
    void sessionChanged();
    void selectionColorChanged();
protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
private:
    void relayout();
    QPointer<EquationSession> m_session;
    Math::EquationLayout m_layout;
    QPointF m_origin;
    QColor m_selectionColor;
    QString m_preedit;
};
}
