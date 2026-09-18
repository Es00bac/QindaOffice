// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc window's host (conversion plan §5.2): the workbook
// document, the sheet list, the formula bar and name box, the Insert /
// Format / Data / Sheet command sets, find and replace, the print
// paginator and the assistant. The QML window (qml/CalcWindow.qml)
// renders the grid through a SpreadsheetItem the host is handed at
// start-up (attachGrid). Every dialog is a request signal the window
// answers through an invokable; the host holds no widgets.
//
// Files: calc_host.cpp (document, sheets, grid, status, name box),
// calc_host_structure.cpp (rows/columns/merge/borders/sort/fill/AutoSum/
// freeze/sheets + the menus), calc_host_rules.cpp (conditional formatting,
// validation, autofilter, comments), calc_host_format.cpp (the cell format
// commands and the strip's combos), calc_host_text.cpp (find/replace,
// templates, print, charts), calc_host_ai.cpp (the assistant).

#include "assistant_controller.h"
#include "document_host.h"
#include "spreadsheet_document.h"
#include "spreadsheet_item.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Ai {
class ModelCatalog;
}

namespace QindaOffice::Apps::QindaCalc {

class CalcHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QStringList sheetNames READ sheetNames NOTIFY sheetsChanged)
    Q_PROPERTY(int activeSheet READ activeSheet WRITE setActiveSheet NOTIFY sheetsChanged)
    Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorChanged)
    Q_PROPERTY(QString formulaText READ formulaText NOTIFY cursorChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QStringList fontSizes READ fontSizes CONSTANT)
    Q_PROPERTY(QVariantList numberFormats READ numberFormats CONSTANT)
    Q_PROPERTY(QString cellFontFamily READ cellFontFamily NOTIFY cursorChanged)
    Q_PROPERTY(QString cellFontSize READ cellFontSize NOTIFY cursorChanged)
    Q_PROPERTY(int cellNumberFormatIndex READ cellNumberFormatIndex NOTIFY cursorChanged)
    Q_PROPERTY(bool chartsAvailable READ chartsAvailable CONSTANT)
    // [{index, title, kind, anchorRow, anchorCol, widthPx, heightPx}]
    Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
public:
    CalcHost();
    ~CalcHost() override;

    [[nodiscard]] SpreadsheetDocument *spreadsheetDocument() const { return m_document; }
    [[nodiscard]] SpreadsheetItem *grid() const { return m_grid; }

    // The QML grid item, once (CalcGrid.qml's Component.onCompleted).
    Q_INVOKABLE void attachGrid(SpreadsheetItem *grid);

    // ---- sheets ----
    [[nodiscard]] QStringList sheetNames() const;
    [[nodiscard]] int activeSheet() const;
    void setActiveSheet(int index);
    Q_INVOKABLE void moveSheet(int from, int to);
    Q_INVOKABLE void renameSheet(int index, const QString &name);
    Q_INVOKABLE void deleteSheetConfirmed(int index);
    // The sheet-tab context menu acts on the tab under the pointer.
    Q_INVOKABLE void activateSheetForMenu(int index);

    // ---- formula bar and name box ----
    [[nodiscard]] QString cursorText() const;
    [[nodiscard]] QString formulaText() const;
    Q_INVOKABLE void commitFormulaBar(const QString &text);
    Q_INVOKABLE void commitNameBox(const QString &text);

    // ---- format strip (calc_host_format.cpp) ----
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontSizes() const;
    [[nodiscard]] QVariantList numberFormats() const;
    [[nodiscard]] QString cellFontFamily() const;
    [[nodiscard]] QString cellFontSize() const;
    [[nodiscard]] int cellNumberFormatIndex() const;
    Q_INVOKABLE void applyFontFamily(const QString &family);
    Q_INVOKABLE void applyFontSize(const QString &size);
    Q_INVOKABLE void applyNumberFormat(int index);
    Q_INVOKABLE void applyTextColor(const QColor &color);
    Q_INVOKABLE void applyFillColor(const QColor &color);

    // ---- structure dialog answers (calc_host_structure.cpp) ----
    Q_INVOKABLE void applyRowHeight(qreal px);
    Q_INVOKABLE void applyColumnWidth(qreal px);

    // ---- rule dialog answers (calc_host_rules.cpp) ----
    // Rules as maps: {range:"A1:B9", op:int, first, second, text,
    // background:"#rrggbb"|"", foreground, bold, italic}.
    Q_INVOKABLE QVariantList condFormatRules() const;
    Q_INVOKABLE void setCondFormatRules(const QVariantList &rules);
    Q_INVOKABLE QString selectionRangeText() const;
    // {kind:int(-1 = none), min, max, list:"a, b", message}
    Q_INVOKABLE QVariantMap validationAtCursor() const;
    Q_INVOKABLE void setValidationRule(const QVariantMap &rule);
    Q_INVOKABLE QString commentAtCursor() const;
    Q_INVOKABLE void setCommentText(const QString &text);
    Q_INVOKABLE void applyFilterValues(int column, const QStringList &kept);

    // ---- text (calc_host_text.cpp) ----
    Q_INVOKABLE bool findNext(const QString &needle, bool caseSensitive);
    Q_INVOKABLE bool findPrevious(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void replaceCurrent(const QString &needle, const QString &replacement, bool caseSensitive);
    Q_INVOKABLE int replaceAll(const QString &needle, const QString &replacement, bool caseSensitive);
    Q_INVOKABLE void loadTemplateResolved(const QString &path, const QString &choice);
    // ---- charts ----
    [[nodiscard]] bool chartsAvailable() const;
    [[nodiscard]] QVariantList charts() const;
    // {kind:int, title, rangeText, headerH, headerV} → the document.
    Q_INVOKABLE void insertChartSpec(const QVariantMap &spec);
    // Series data for a chart preview / window (chart_series.h).
    Q_INVOKABLE QVariantMap chartSeries(const QVariantMap &spec) const;
    Q_INVOKABLE QVariantMap chartSeriesAt(int index) const;

    [[nodiscard]] Ai::AssistantController *assistant() const { return m_assistant; }

signals:
    void sheetsChanged();
    void cursorChanged();
    void chartsChanged();
    // Dialogs the window answers (see the invokables above).
    void rowHeightRequested(qreal current);
    void columnWidthRequested(qreal current);
    void renameSheetRequested(int index, const QString &name);
    void deleteSheetRequested(int index, const QString &name);
    void condFormatRequested();
    void validationRequested();
    void commentRequested(const QString &cell, const QString &text);
    void filterMenuRequested(int column, const QStringList &values, const QStringList &allowed,
                             qreal x, qreal y);
    void contextMenuRequested(qreal x, qreal y);
    void chartDialogRequested(const QString &initialRange);
    void templateConsentRequested(const QString &path);
    void findRequested(bool replace);
    void assistantToggleRequested(bool visible);

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // calc_host.cpp
    void wireEditCommands();
    void wireGrid();
    void updateSelectionStatus();
    void syncFromCursor();
    void goToRange(const CalcEng::RangeRef &ref);
    void commitInput(const CalcEng::CellAddr &addr, const QString &input);
    [[nodiscard]] CalcEng::CellRange selection() const;
    [[nodiscard]] CalcEng::CellAddr cursor() const;
    [[nodiscard]] const CalcEng::Sheet &sheet() const;
    void refreshGrid();
    // calc_host_structure.cpp
    void createStructureActions();
    void insertMenus();
    void syncStructureFromCursor();
    void applyBorders(int bits, bool outlineOnly);
    void sortSelection(bool ascending);
    void autoSum();
    void toggleFreeze(bool on);
    [[nodiscard]] CalcEng::CellRange blockAroundSelection() const;
    // calc_host_rules.cpp
    void createRuleActions();
    void syncRulesFromCursor();
    void toggleFilter(bool on);
    // calc_host_format.cpp
    void createFormatActions();
    void syncFormatFromCursor();
    void applyPatch(const StylePatch &patch, const QString &undoText);
    // calc_host_text.cpp
    void createTextActions();
    [[nodiscard]] QList<CalcEng::CellAddr> matches(const QString &needle, bool caseSensitive) const;
    void loadTemplate(const QString &path);
    // calc_host_ai.cpp
    void createAssistant();

    SpreadsheetDocument *m_document = nullptr;
    QPointer<SpreadsheetItem> m_grid;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    QString m_pendingTemplate;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::QindaCalc
