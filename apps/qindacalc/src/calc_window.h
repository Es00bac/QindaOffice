// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc window: the shared document frame (menus, save consent,
// journal, global menu) hosting the spreadsheet view, formula bar, name
// box and sheet tab bar (plan section 6.2 v1 scope).

#include "document_window.h"
#include "print_helper.h"

#include "value.h"
#include "parser.h"

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
    QString saveFilterString() const override;

private:
    void rebuildSheetTabs();
    void createCalcActions();
    void wireEditingActions();
    void commitInput(const CalcEng::CellAddr &addr, const QString &input);
    // Name-box entry: navigate to a reference or an existing name, or define
    // a new name over the current selection.
    void commitNameBox(const QString &text);
    void goToRange(const CalcEng::RangeRef &ref);
    // Paginating renderer shared by Print and Print Preview.
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;
    // Save/Discard/Cancel consent before a template replaces the workbook.
    [[nodiscard]] bool confirmDiscardChanges();
    void insertChart();
    void rebuildChartWindows();
    // Re-renders the chart inside each already-open chart window. Used for
    // data edits, which must not destroy and re-create the windows.
    void refreshChartWindows();

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
