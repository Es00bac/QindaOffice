// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A left-to-right layout that wraps to a new row when the next item would
// not fit (the standard Qt flow-layout shape).
//
// Why the suite needs one: QToolBar does not wrap. When its items overrun
// the window it hides the overflow behind an extension chevron, so commands
// the user never opens that menu for are simply invisible — the formatting
// strip lost Align Left/Center/Right that way. A toolbar is a surface for
// commands you can see; anything it has to hide is a layout failure, not a
// feature.
//
// This is a layout, not a style: ADR-0116 (stock Qt 6) and AGENTS.md rule 3
// govern painting and theming, neither of which this touches.

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStyle>

namespace QindaOffice::Shell {

class FlowLayout final : public QLayout {
    Q_OBJECT
public:
    explicit FlowLayout(QWidget *parent = nullptr, int margin = 0, int hSpacing = 4,
                        int vSpacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    [[nodiscard]] int horizontalSpacing() const { return m_hSpace; }
    [[nodiscard]] int verticalSpacing() const { return m_vSpace; }
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] int count() const override { return int(m_items.size()); }
    [[nodiscard]] QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    [[nodiscard]] QSize sizeHint() const override { return minimumSize(); }

private:
    // Lays the items out inside `rect`; returns the total height used.
    // `apply` false measures without moving anything (heightForWidth).
    int doLayout(const QRect &rect, bool apply) const;

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};

} // namespace QindaOffice::Shell
