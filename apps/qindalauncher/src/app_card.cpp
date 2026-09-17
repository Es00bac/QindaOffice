// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_card.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace QindaOffice::Launcher {

AppCard::AppCard(AppEntry app, QWidget *parent)
    : QWidget(parent)
    , m_app(std::move(app))
{
    setObjectName(QStringLiteral("appCard_%1").arg(m_app.id));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    // The blurb is the accessible description; the name is the label. A
    // painted card with no accessible identity is invisible to a screen
    // reader, which would undo the M10 accessibility work elsewhere.
    setAccessibleName(m_app.name);
    setAccessibleDescription(m_app.blurb);
    setToolTip(m_app.blurb);
}

QSize AppCard::sizeHint() const
{
    return {220, 190};
}

QSize AppCard::minimumSizeHint() const
{
    return {160, 160};
}

void AppCard::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(available ? m_app.blurb
                         : tr("%1 is not installed").arg(m_app.name));
    update();
}

void AppCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);

    // The card face lifts on hover and presses in on click; both states are
    // palette blends so the whole thing follows the desktop's theme.
    QColor face = palette().base().color();
    if (!m_available) {
        face = palette().window().color();
    } else if (m_pressed) {
        face = palette().highlight().color().lighter(185);
    } else if (m_hovered) {
        face = palette().alternateBase().color();
    }
    QPainterPath path;
    path.addRoundedRect(card, kRadius, kRadius);
    if (m_available && (m_hovered || m_pressed)) {
        // A soft drop shadow under a hovered card: the one bit of depth
        // that makes a flat grid read as something to click.
        QColor shadow = palette().shadow().color();
        shadow.setAlpha(40);
        painter.fillPath(path.translated(0, 2), shadow);
    }
    painter.fillPath(path, face);
    QPen border(hasFocus() ? palette().highlight().color() : palette().mid().color());
    border.setWidthF(hasFocus() ? 2.0 : 1.0);
    painter.setPen(border);
    painter.drawPath(path);

    const QIcon icon = iconFor(m_app);
    const QRect iconRect(int(card.center().x() - kIconPx / 2), int(card.top()) + kPadding,
                         kIconPx, kIconPx);
    if (!icon.isNull()) {
        icon.paint(&painter, iconRect, Qt::AlignCenter,
                   m_available ? QIcon::Normal : QIcon::Disabled);
    } else {
        // No themed icon (a bare offscreen run, a minimal theme): a tile
        // with the app's initial, never a blank square.
        QPainterPath tile;
        tile.addRoundedRect(QRectF(iconRect), kRadius, kRadius);
        painter.fillPath(tile, palette().mid().color());
        painter.setPen(palette().brightText().color());
        QFont initial = font();
        initial.setPointSizeF(font().pointSizeF() * 2.2);
        initial.setBold(true);
        painter.setFont(initial);
        painter.drawText(iconRect, Qt::AlignCenter, m_app.name.mid(5, 1));
    }

    painter.setPen(m_available ? palette().text().color() : palette().mid().color());
    QFont title = font();
    title.setBold(true);
    title.setPointSizeF(font().pointSizeF() + 1.5);
    painter.setFont(title);
    const QRect nameRect(int(card.left()) + kPadding, iconRect.bottom() + 10,
                         int(card.width()) - 2 * kPadding, QFontMetrics(title).height() + 2);
    painter.drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter, m_app.name);

    painter.setFont(font());
    painter.setPen(palette().placeholderText().color());
    const QRect roleRect(nameRect.left(), nameRect.bottom() + 2, nameRect.width(),
                         int(card.bottom()) - nameRect.bottom() - kPadding);
    painter.drawText(roleRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                     m_available ? m_app.role : tr("Not installed"));
}

void AppCard::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void AppCard::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QWidget::leaveEvent(event);
}

void AppCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_available) {
        m_pressed = true;
        setFocus(Qt::MouseFocusReason);
        update();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AppCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed && event->button() == Qt::LeftButton) {
        m_pressed = false;
        update();
        // Only when the release lands on the card: dragging off is a
        // cancel, the way every button behaves.
        if (rect().contains(event->position().toPoint())) {
            emit activated(m_app);
        }
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void AppCard::keyPressEvent(QKeyEvent *event)
{
    if (m_available
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space)) {
        emit activated(m_app);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AppCard::focusInEvent(QFocusEvent *event)
{
    update();
    QWidget::focusInEvent(event);
}

void AppCard::focusOutEvent(QFocusEvent *event)
{
    update();
    QWidget::focusOutEvent(event);
}

} // namespace QindaOffice::Launcher
