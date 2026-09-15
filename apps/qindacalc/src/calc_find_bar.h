// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc find bar (M12): searches cell inputs and display text of the
// active sheet in row-major order, wraps around, selects the match and
// scrolls it into view. Driven by the shell's standard find actions.

#include <QWidget>

class QCheckBox;
class QLineEdit;

namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;
class SpreadsheetView;

class CalcFindBar final : public QWidget {
    Q_OBJECT
public:
    CalcFindBar(SpreadsheetDocument *document, SpreadsheetView *view, QWidget *parent);

    void openFind();
    void findNext();
    void findPrevious();

signals:
    void message(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] bool findIn(bool backward);

    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    QLineEdit *m_findEdit;
    QCheckBox *m_caseCheck;
};

} // namespace QindaOffice::Apps::QindaCalc
