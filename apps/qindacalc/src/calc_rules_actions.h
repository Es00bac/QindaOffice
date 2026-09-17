// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc's rule commands: conditional formatting, data validation, the
// autofilter and cell comments — the four things a spreadsheet is expected
// to be able to say about a range rather than about one cell's value.
//
// Same action discipline as the rest of the suite: persistent window-scoped
// QActions with stable objectNames, shared by the Format/Data/Insert menus
// and the grid's context menu.

#include "sheet_rules.h"

#include <QAction>
#include <QObject>

class QMenu;
class QPoint;

namespace QindaOffice::Shell {
class DocumentWindow;
}

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;
class SpreadsheetView;

class CalcRuleActions final : public QObject {
    Q_OBJECT
public:
    CalcRuleActions(Shell::DocumentWindow *window, SpreadsheetDocument *document,
                    SpreadsheetView *view);

    void populateFormatMenu(QMenu *menu);
    void populateDataMenu(QMenu *menu);
    void populateContextMenu(QMenu *menu);
    [[nodiscard]] QList<QAction *> stripActions() const;
    void syncFromCursor();

    // The dropdown the grid's filter button opens.
    void showFilterMenu(qint32 column, const QPoint &globalPos);

private:
    void createActions();
    void editCondFormats();
    void editValidation();
    void toggleFilter(bool on);
    void editComment();
    void deleteComment();
    // The contiguous block around the cursor, used when the user has not
    // selected one themselves — the same convention Sort already uses.
    [[nodiscard]] CalcEng::CellRange blockAroundSelection() const;
    [[nodiscard]] QString displayTextAt(const CalcEng::CellAddr &addr) const;

    Shell::DocumentWindow *m_window;
    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    QAction *m_condFormat = nullptr;
    QAction *m_validation = nullptr;
    QAction *m_filter = nullptr;
    QAction *m_comment = nullptr;
    QAction *m_deleteComment = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::QindaCalc
