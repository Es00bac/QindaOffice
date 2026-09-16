// SPDX-License-Identifier: GPL-3.0-or-later
#include "flow_layout.h"

#include <QWidget>

namespace QindaOffice::Shell {

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent)
    , m_hSpace(hSpacing)
    , m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), false);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, true);
}

QSize FlowLayout::minimumSize() const
{
    // The narrowest the strip can get is its widest single item: below that
    // an item would be clipped, which is the failure this layout exists to
    // prevent. Height is left to heightForWidth.
    QSize size;
    for (const QLayoutItem *item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool apply) const
{
    const QMargins margins = contentsMargins();
    const QRect content = rect.adjusted(margins.left(), margins.top(), -margins.right(),
                                        -margins.bottom());
    int x = content.x();
    int y = content.y();
    int rowHeight = 0;
    for (QLayoutItem *item : m_items) {
        const QSize hint = item->sizeHint();
        int next = x + hint.width();
        if (rowHeight > 0 && next > content.right() + 1) {
            // Wrap: this item would overrun the row, so start a new one.
            x = content.x();
            y += rowHeight + m_vSpace;
            next = x + hint.width();
            rowHeight = 0;
        }
        if (apply) {
            item->setGeometry(QRect(QPoint(x, y), hint));
        }
        x = next + m_hSpace;
        rowHeight = qMax(rowHeight, hint.height());
    }
    return y + rowHeight - rect.y() + margins.bottom();
}

} // namespace QindaOffice::Shell
