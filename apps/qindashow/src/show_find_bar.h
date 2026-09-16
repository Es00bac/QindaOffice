// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow find bar: searches every slide's title, shape text and speaker
// notes in deck order, wraps around, and selects the matching slide.
//
// Edit > Find/Find Next/Find Previous were created disabled and never wired
// in QindaShow, so the menu offered three commands the app did not have.
// Same shape as QindaCalc's bar (see calc_find_bar.h): a line edit, next,
// previous, case toggle, close.

#include <QWidget>

class QCheckBox;
class QLineEdit;

namespace QindaOffice::Apps::Show {

class ShowWindow;

class ShowFindBar final : public QWidget {
    Q_OBJECT
public:
    ShowFindBar(ShowWindow *window, QWidget *parent);

    void openFind();
    void findNext();
    void findPrevious();

signals:
    void message(const QString &text);
    // The slide the search landed on; the window selects it.
    void slideRequested(int row);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    [[nodiscard]] bool findIn(bool backward);
    // Deck-order indices of slides whose title, shape text or notes contain
    // the needle.
    [[nodiscard]] QList<int> matches() const;

    ShowWindow *m_window;
    QLineEdit *m_findEdit;
    QCheckBox *m_caseCheck;
};

} // namespace QindaOffice::Apps::Show
