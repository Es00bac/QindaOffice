// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc window: the shared document frame (menus, save consent,
// journal, global menu) hosting the spreadsheet view, formula bar, name
// box and sheet tab bar (plan section 6.2 v1 scope).

#include "document_window.h"

#include "value.h"

class QLineEdit;
class QTabBar;
class QToolBar;

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;
class SpreadsheetView;
class CalcFindBar;
class CalcFormatController;

class CalcWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    CalcWindow();
    ~CalcWindow() override;

    [[nodiscard]] SpreadsheetDocument *spreadsheetDocument() const { return m_document; }
    [[nodiscard]] SpreadsheetView *spreadsheetView() const { return m_view; }
    [[nodiscard]] CalcFindBar *findBar() const { return m_findBar; }
    [[nodiscard]] CalcFormatController *formatController() const { return m_format; }

    void loadWorkbookPath(const QString &path);

protected:
    void insertApplicationMenus(QMenu *before) override;
    void insertFileMenuActions(QMenu *fileMenu) override;
    QString filterString() const override;

private:
    void rebuildSheetTabs();
    void createCalcActions();
    void wireEditingActions();
    void commitInput(const CalcEng::CellAddr &addr, const QString &input);
    void insertChart();
    void rebuildChartWindows();

    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    QLineEdit *m_nameBox;
    QLineEdit *m_formulaBar;
    QTabBar *m_tabs;
    CalcFindBar *m_findBar = nullptr;
    CalcFormatController *m_format = nullptr;
    QAction *m_insertChartAction = nullptr;
    QAction *m_insertSheetAction = nullptr;
    QAction *m_thousandsAction = nullptr;
    bool m_syncingBar = false;
    QList<class QWidget *> m_chartWindows;
};

} // namespace QindaOffice::Apps::QindaCalc
