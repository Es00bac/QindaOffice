// SPDX-License-Identifier: GPL-3.0-or-later
#include "wrapping_tool_bar.h"

#include "flow_layout.h"

#include <QAction>
#include <QFrame>
#include <QResizeEvent>
#include <QToolButton>

namespace QindaOffice::Shell {

WrappingToolBar::WrappingToolBar(const QString &title, QWidget *parent)
    : QToolBar(title, parent)
{
    m_strip = new QWidget(this);
    m_flow = new FlowLayout(m_strip, /*margin=*/2, /*hSpacing=*/2, /*vSpacing=*/2);
    m_strip->setLayout(m_flow);
    // The strip takes the toolbar's full width; without Expanding the
    // toolbar layout would give it only its sizeHint and it would never wrap.
    m_strip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    addWidget(m_strip);
}

void WrappingToolBar::addStripAction(QAction *action)
{
    auto *button = new QToolButton(m_strip);
    button->setDefaultAction(action);
    // AGENT-GUARD: icon-only ONLY when the theme actually resolved an icon.
    // The qindaqt icon theme inherits hicolor alone, so a name it does not
    // carry comes back null and an icon-only button would render blank.
    button->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly
                                                       : Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    if (button->toolTip().isEmpty()) {
        button->setToolTip(action->text());
    }
    m_flow->addWidget(button);
    m_stripActions.append(action);
}

void WrappingToolBar::addStripWidget(QWidget *widget)
{
    widget->setParent(m_strip);
    m_flow->addWidget(widget);
}

void WrappingToolBar::addStripSeparator()
{
    auto *line = new QFrame(m_strip);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    m_flow->addWidget(line);
}

void WrappingToolBar::resizeEvent(QResizeEvent *event)
{
    QToolBar::resizeEvent(event);
    retuneHeight();
}

void WrappingToolBar::retuneHeight()
{
    if (m_flow == nullptr || m_strip == nullptr || m_strip->width() <= 0) {
        return;
    }
    const int wrapped = m_flow->heightForWidth(m_strip->width());
    if (wrapped > 0 && m_strip->height() != wrapped) {
        // Fixing the height is what makes the TOOLBAR grow: QToolBar sizes
        // itself from its child's size hint, and a flow layout's hint cannot
        // express "depends on my width" on its own.
        m_strip->setFixedHeight(wrapped);
        updateGeometry();
    }
}

} // namespace QindaOffice::Shell
