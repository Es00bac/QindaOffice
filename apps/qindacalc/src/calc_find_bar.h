// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc find bar (M12): searches cell inputs and display text of the
// active sheet in row-major order, wraps around, selects the match and
// scrolls it into view. Driven by the shell's standard find actions.

#include "value.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QLineEdit;

namespace CalcEng = QindaOffice::Calc;

namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;
class SpreadsheetView;

class CalcFindBar final : public QWidget {
    Q_OBJECT
public:
    CalcFindBar(SpreadsheetDocument *document, SpreadsheetView *view, QWidget *parent);

    void openFind();
    // Same bar with the replace row revealed (Ctrl+H). Find-only mode hides
    // the row so the common case stays uncluttered.
    void openReplace();
    void findNext();
    void findPrevious();
    // Replaces the match under the cursor and advances; no-op when the
    // cursor is not on a match.
    void replaceCurrent();
    // One undo step for the whole sheet.
    void replaceAll();

signals:
    void message(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] bool findIn(bool backward);
    // Cells whose input or display text contains the needle, row-major.
    [[nodiscard]] QList<CalcEng::CellAddr> matches() const;
    void setReplaceVisible(bool visible);

    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QCheckBox *m_caseCheck;
    QList<QWidget *> m_replaceRow;
};

} // namespace QindaOffice::Apps::QindaCalc
