// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One app's card on the launcher: its mascot, its name, what it is for, and
// a click that starts it.
//
// Painted rather than assembled from labels so the whole card is one hover
// and one click target, and so the icon can be as large as the mascots
// deserve. Every colour comes from the palette (AGENTS.md rule 3: no QSS,
// no palette literals) — the card reads correctly on a light or a dark
// desktop because it never names a colour of its own.

#include "launcher_catalog.h"

#include <QWidget>

namespace QindaOffice::Launcher {

class AppCard final : public QWidget {
    Q_OBJECT
public:
    AppCard(AppEntry app, QWidget *parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] const AppEntry &app() const { return m_app; }
    // False when the app's binary cannot be found; the card then says so
    // instead of looking clickable and doing nothing.
    void setAvailable(bool available);

signals:
    void activated(const AppEntry &app);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    AppEntry m_app;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_available = true;

    static constexpr int kIconPx = 72;
    static constexpr int kRadius = 12;
    static constexpr int kPadding = 16;
};

} // namespace QindaOffice::Launcher
