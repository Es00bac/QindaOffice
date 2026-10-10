// SPDX-License-Identifier: GPL-3.0-or-later
// BaseHost: the QindaBase commands (ActionEntries), menus and enablement.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon and the keyboard — never rename
// or reuse one; add new ids for new commands. An id appears in the menu
// tree exactly once (the desktop validator rejects duplicates).
#include "base_document.h"
#include "base_host.h"

#include "standard_actions.h"

#include <QKeySequence>
#include <QUndoStack>

#include <iterator>

namespace QindaOffice::Apps::Base {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
};

// Every command the QML answers (actionRequested(id)).
constexpr Command kCommands[] = {
    // Views and objects
    {"baseOpenObjectAction", QT_TRANSLATE_NOOP("BaseHost", "&Open"), "", "external-link"},
    {"baseViewDesignAction", QT_TRANSLATE_NOOP("BaseHost", "&Design View"), "", "square-pen"},
    {"baseViewSqlAction", QT_TRANSLATE_NOOP("BaseHost", "S&QL View"), "", "code"},
    {"baseRenameObjectAction", QT_TRANSLATE_NOOP("BaseHost", "Re&name…"), "F2", "text-cursor-input"},
    {"baseDeleteObjectAction", QT_TRANSLATE_NOOP("BaseHost", "De&lete Object…"), "", "trash-2"},
    // Records
    {"baseNewRecordAction", QT_TRANSLATE_NOOP("BaseHost", "&New Record"), "Ctrl+Shift+N", "list-plus"},
    {"baseSaveRecordAction", QT_TRANSLATE_NOOP("BaseHost", "Sa&ve Record"), "Shift+Return", "check"},
    {"baseDeleteRecordAction", QT_TRANSLATE_NOOP("BaseHost", "&Delete Record…"), "Ctrl+Delete", "list-minus"},
    {"baseRefreshAction", QT_TRANSLATE_NOOP("BaseHost", "Re&fresh"), "F5", "refresh-cw"},
    // Sort and filter
    {"baseSortAscendingAction", QT_TRANSLATE_NOOP("BaseHost", "Sort &Ascending"), "", "arrow-up-a-z"},
    {"baseSortDescendingAction", QT_TRANSLATE_NOOP("BaseHost", "Sort D&escending"), "", "arrow-down-z-a"},
    {"baseFilterBySelectionAction", QT_TRANSLATE_NOOP("BaseHost", "Filter by &Selection"), "", "funnel"},
    {"baseFilterExcludingAction", QT_TRANSLATE_NOOP("BaseHost", "Filter &Excluding Selection"), "", "circle-off"},
    {"baseClearFiltersAction", QT_TRANSLATE_NOOP("BaseHost", "&Clear Sort and Filters"), "", "eraser"},
    // Create
    {"baseNewTableAction", QT_TRANSLATE_NOOP("BaseHost", "&Table…"), "", "table"},
    {"baseTableDesignAction", QT_TRANSLATE_NOOP("BaseHost", "Table &Design"), "", "table-2"},
    {"baseQueryDesignAction", QT_TRANSLATE_NOOP("BaseHost", "&Query Design"), "", "funnel"},
    {"baseNewFormAction", QT_TRANSLATE_NOOP("BaseHost", "&Form"), "", "layout-template"},
    {"baseFormWizardAction", QT_TRANSLATE_NOOP("BaseHost", "Form &Wizard…"), "", "wand-sparkles"},
    {"baseNewReportAction", QT_TRANSLATE_NOOP("BaseHost", "&Report"), "", "scroll-text"},
    {"baseReportWizardAction", QT_TRANSLATE_NOOP("BaseHost", "Report Wi&zard…"), "", "wand"},
    // External data
    {"baseImportCsvAction", QT_TRANSLATE_NOOP("BaseHost", "Import &Text or CSV…"), "", "file-input"},
    {"baseImportAccessAction", QT_TRANSLATE_NOOP("BaseHost", "Import &Access Database…"), "", "import"},
    {"baseImportExcelAction", QT_TRANSLATE_NOOP("BaseHost", "Import Exc&el Workbook…"), "", "sheet"},
    {"baseExportCsvAction", QT_TRANSLATE_NOOP("BaseHost", "Export as &CSV…"), "", "file-output"},
    {"baseExportXlsxAction", QT_TRANSLATE_NOOP("BaseHost", "Export as E&xcel Workbook…"), "", "file-spreadsheet"},
    {"baseExportPdfAction", QT_TRANSLATE_NOOP("BaseHost", "Export Report as &PDF…"), "", "file-down"},
    // File fields (M65): the datasheet's current cell or the form's chosen field
    {"baseFileInsertAction", QT_TRANSLATE_NOOP("BaseHost", "&Insert File…"), "", "paperclip"},
    {"baseFileSaveAction", QT_TRANSLATE_NOOP("BaseHost", "Save Fi&le As…"), "", "download"},
    {"baseFileRemoveAction", QT_TRANSLATE_NOOP("BaseHost", "Re&move File"), "", "file-minus"},
    // Database tools
    {"baseRelationshipsAction", QT_TRANSLATE_NOOP("BaseHost", "&Relationships"), "", "git-branch"},
    {"baseSqlConsoleAction", QT_TRANSLATE_NOOP("BaseHost", "&SQL"), "", "terminal"},
    {"baseCompactRepairAction", QT_TRANSLATE_NOOP("BaseHost", "&Compact and Repair…"), "", "wrench"},
    {"baseCheckIntegrityAction", QT_TRANSLATE_NOOP("BaseHost", "Check &Integrity"), "", "circle-check"},
    {"baseBackupAction", QT_TRANSLATE_NOOP("BaseHost", "&Back Up Database"), "", "archive"},
    {"baseDatabasePropertiesAction", QT_TRANSLATE_NOOP("BaseHost", "Database &Properties…"), "", "database"},
    // Table design (contextual)
    {"baseDesignAddFieldAction", QT_TRANSLATE_NOOP("BaseHost", "&Add Field"), "", "plus"},
    {"baseDesignDeleteFieldAction", QT_TRANSLATE_NOOP("BaseHost", "D&elete Field"), "", "minus"},
    {"baseDesignMoveUpAction", QT_TRANSLATE_NOOP("BaseHost", "Move Field &Up"), "", "arrow-up"},
    {"baseDesignMoveDownAction", QT_TRANSLATE_NOOP("BaseHost", "Move Field Do&wn"), "", "arrow-down"},
    {"baseDesignPrimaryKeyAction", QT_TRANSLATE_NOOP("BaseHost", "&Primary Key"), "", "key-round"},
    {"baseDesignApplyAction", QT_TRANSLATE_NOOP("BaseHost", "&Preview and Apply…"), "", "check-check"},
    {"baseDesignDiscardAction", QT_TRANSLATE_NOOP("BaseHost", "&Discard Changes"), "", "undo"},
    // Query design (contextual)
    {"baseQueryRunAction", QT_TRANSLATE_NOOP("BaseHost", "&Run"), "Ctrl+R", "play"},
    {"baseQueryAddTableAction", QT_TRANSLATE_NOOP("BaseHost", "Add &Table…"), "", "table"},
    {"baseQueryClearAction", QT_TRANSLATE_NOOP("BaseHost", "&Clear Grid"), "", "eraser"},
    // Form and report design (contextual)
    {"baseFormPreviewAction", QT_TRANSLATE_NOOP("BaseHost", "Pre&view Layout"), "", "eye"},
    {"baseReportRefreshAction", QT_TRANSLATE_NOOP("BaseHost", "Refresh Re&port"), "", "refresh-ccw"},
    // M80 (D-089): Access's macros, labels, XML, validation, query
    // parameters and form controls.
    {"baseNewMacroAction", QT_TRANSLATE_NOOP("BaseHost", "&Macro"), "", "workflow"},
    {"baseLabelsWizardAction", QT_TRANSLATE_NOOP("BaseHost", "&Labels…"), "", "tag"},
    {"baseExportXmlAction", QT_TRANSLATE_NOOP("BaseHost", "Export as X&ML…"), "", "file-type"},
    {"baseDesignTestRulesAction", QT_TRANSLATE_NOOP("BaseHost", "&Test Validation Rules"), "", "list-checks"},
    {"baseQueryParametersAction", QT_TRANSLATE_NOOP("BaseHost", "&Parameters…"), "", "circle-question-mark"},
    {"baseFormAddSubformAction", QT_TRANSLATE_NOOP("BaseHost", "Add &Subform…"), "", "layers"},
    {"baseFormAddButtonAction", QT_TRANSLATE_NOOP("BaseHost", "Add &Button…"), "", "mouse-pointer-2"},
    // Macro design (contextual, M80)
    {"baseMacroRunAction", QT_TRANSLATE_NOOP("BaseHost", "&Run Macro"), "", "play"},
    {"baseMacroAddActionAction", QT_TRANSLATE_NOOP("BaseHost", "&Add Action"), "", "list-plus"},
    {"baseMacroDeleteActionAction", QT_TRANSLATE_NOOP("BaseHost", "&Delete Action"), "", "list-minus"},
    {"baseMacroMoveUpAction", QT_TRANSLATE_NOOP("BaseHost", "Move Action &Up"), "", "arrow-up"},
    {"baseMacroMoveDownAction", QT_TRANSLATE_NOOP("BaseHost", "Move Action Do&wn"), "", "arrow-down"},
};

// Toggles.
constexpr Command kToggles[] = {
    {"baseTotalsRowAction", QT_TRANSLATE_NOOP("BaseHost", "&Totals Row"), "", "sigma"},
    {"baseFilterAction", QT_TRANSLATE_NOOP("BaseHost", "&Filter Bar"), "Ctrl+Shift+L", "sliders-horizontal"},
    {"baseQueryTotalsAction", QT_TRANSLATE_NOOP("BaseHost", "&Totals"), "", "sigma"},
    {"baseShowNavigationAction", QT_TRANSLATE_NOOP("BaseHost", "&Navigation Pane"), "F11", "panel-left"},
    {"baseReportGroupSortAction", QT_TRANSLATE_NOOP("BaseHost", "&Group, Sort and Total"), "", "list-tree"}, // M80
};

// The query type, a radio group mirrored from the designer's design.
constexpr Command kQueryTypes[] = {
    {"baseQueryTypeSelectAction", QT_TRANSLATE_NOOP("BaseHost", "&Select Query"), "", "table-2"},
    {"baseQueryTypeUpdateAction", QT_TRANSLATE_NOOP("BaseHost", "&Update Query"), "", "pencil"},
    {"baseQueryTypeDeleteAction", QT_TRANSLATE_NOOP("BaseHost", "&Delete Query"), "", "trash"},
    {"baseQueryTypeAppendAction", QT_TRANSLATE_NOOP("BaseHost", "&Append Query"), "", "copy-plus"},
    {"baseQueryTypeCrosstabAction", QT_TRANSLATE_NOOP("BaseHost", "&Crosstab Query"), "", "grid-3x3"},     // M80
    {"baseQueryTypeMakeTableAction", QT_TRANSLATE_NOOP("BaseHost", "&Make Table Query"), "", "layout-grid"}, // M80
};

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

MenuNode a(const char *raw)
{
    return MenuNode::action(QString::fromLatin1(raw));
}

} // namespace

void BaseHost::createActions()
{
    for (const Command &c : kCommands) {
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
        connect(entry, &ActionEntry::triggered, this, [this, raw = c.id] { emit actionRequested(id(raw)); });
    }
    for (const Command &c : kToggles) {
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
        entry->setCheckable(true);
        connect(entry, &ActionEntry::triggered, this, [this, raw = c.id] { emit actionRequested(id(raw)); });
    }
    action(id("baseShowNavigationAction"))->setChecked(true);
    connect(action(id("baseShowNavigationAction")), &ActionEntry::toggled, this, [this](bool on) {
        m_navigationVisible = on;
        emit navigationChanged();
    });
    for (const Command &c : kQueryTypes) {
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("baseQueryType"));
        connect(entry, &ActionEntry::triggered, this, [this, raw = c.id] { emit actionRequested(id(raw)); });
    }
    action(id("baseQueryTypeSelectAction"))->setChecked(true);
    action(id(ActionId::kFileNew))->setText(tr("&New Database…"));

    // Standard commands QindaBase answers.
    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    on(ActionId::kEditUndo, [this] {
        if (m_designHistory) {
            emit actionRequested(id(ActionId::kEditUndo));
        } else {
            m_document->undoStack()->undo();
        }
    });
    on(ActionId::kEditRedo, [this] {
        if (m_designHistory) {
            emit actionRequested(id(ActionId::kEditRedo));
        } else {
            m_document->undoStack()->redo();
        }
    });
    for (const char *raw : {ActionId::kEditCut, ActionId::kEditCopy, ActionId::kEditPaste, ActionId::kEditSelectAll,
                            ActionId::kEditFind, ActionId::kEditFindNext, ActionId::kEditFindPrevious}) {
        on(raw, [this, raw] { emit actionRequested(id(raw)); });
    }
    on(ActionId::kViewZoomIn, [this] {
        m_zoom = std::min(3.0, m_zoom * 1.25);
        emit zoomChanged();
    });
    on(ActionId::kViewZoomOut, [this] {
        m_zoom = std::max(0.25, m_zoom / 1.25);
        emit zoomChanged();
    });
    on(ActionId::kViewZoomReset, [this] {
        m_zoom = 1.0;
        emit zoomChanged();
    });
    createAccessToolActions(); // M86 (base_host_actions_access.cpp)
}

void BaseHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString close = id(ActionId::kFileCloseWindow);
    tree->insertInto(QStringLiteral("fileMenu"), close, a("baseBackupAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("baseDatabasePropertiesAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());
    const QString before = QStringLiteral("aiMenu");
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("baseRecordsMenu"), tr("&Records"),
                                                     {a("baseNewRecordAction"), a("baseSaveRecordAction"),
                                                      a("baseDeleteRecordAction"), a("baseRefreshAction"),
                                                      a("baseTotalsRowAction"), MenuNode::separator(),
                                                      a("baseSortAscendingAction"), a("baseSortDescendingAction"),
                                                      a("baseFilterBySelectionAction"), a("baseFilterExcludingAction"),
                                                      a("baseFilterAction"), a("baseClearFiltersAction"),
                                                      MenuNode::separator(), a("baseFileInsertAction"),
                                                      a("baseFileSaveAction"), a("baseFileRemoveAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("baseCreateMenu"), tr("&Create"),
                                                     {a("baseNewTableAction"), a("baseTableDesignAction"),
                                                      MenuNode::separator(), a("baseQueryDesignAction"),
                                                      MenuNode::separator(), a("baseNewFormAction"),
                                                      a("baseFormWizardAction"), MenuNode::separator(),
                                                      a("baseNewReportAction"), a("baseReportWizardAction"),
                                                      a("baseLabelsWizardAction"), MenuNode::separator(),
                                                      a("baseNewMacroAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("baseExternalMenu"), tr("E&xternal Data"),
                                                     {a("baseImportCsvAction"), a("baseImportExcelAction"),
                                                      a("baseImportAccessAction"),
                                                      MenuNode::separator(), a("baseExportCsvAction"),
                                                      a("baseExportXlsxAction"), a("baseExportXmlAction"),
                                                      a("baseExportPdfAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("baseDesignMenu"), tr("&Design"),
                                                     {a("baseDesignAddFieldAction"), a("baseDesignDeleteFieldAction"),
                                                      a("baseDesignMoveUpAction"), a("baseDesignMoveDownAction"),
                                                      a("baseDesignPrimaryKeyAction"), a("baseDesignTestRulesAction"),
                                                      a("baseDesignApplyAction"), a("baseDesignDiscardAction"),
                                                      MenuNode::separator(), a("baseQueryRunAction"),
                                                      a("baseQueryAddTableAction"), a("baseQueryTotalsAction"),
                                                      a("baseQueryParametersAction"), a("baseQueryTypeSelectAction"),
                                                      a("baseQueryTypeUpdateAction"), a("baseQueryTypeDeleteAction"),
                                                      a("baseQueryTypeAppendAction"), a("baseQueryTypeCrosstabAction"),
                                                      a("baseQueryTypeMakeTableAction"), a("baseQueryClearAction"),
                                                      MenuNode::separator(), a("baseFormPreviewAction"),
                                                      a("baseFormAddSubformAction"), a("baseFormAddButtonAction"),
                                                      a("baseReportRefreshAction"), a("baseReportGroupSortAction"),
                                                      MenuNode::separator(), a("baseMacroRunAction"),
                                                      a("baseMacroAddActionAction"), a("baseMacroDeleteActionAction"),
                                                      a("baseMacroMoveUpAction"), a("baseMacroMoveDownAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("baseToolsMenu"), tr("Database &Tools"),
                                                     {a("baseRelationshipsAction"), a("baseSqlConsoleAction"),
                                                      MenuNode::separator(), a("baseCompactRepairAction"),
                                                      a("baseCheckIntegrityAction")}));
    // View: the object views first, then the standard chrome toggles.
    const QString firstView = id(ActionId::kViewZoomIn);
    for (const char *raw : {"baseOpenObjectAction", "baseViewDesignAction", "baseViewSqlAction"}) {
        tree->insertInto(QStringLiteral("viewMenu"), firstView, a(raw));
    }
    tree->insertInto(QStringLiteral("viewMenu"), firstView, MenuNode::separator());
    tree->insertInto(QStringLiteral("viewMenu"), firstView, a("baseShowNavigationAction"));
    tree->insertInto(QStringLiteral("viewMenu"), firstView, a("baseRenameObjectAction"));
    tree->insertInto(QStringLiteral("viewMenu"), firstView, a("baseDeleteObjectAction"));
    tree->insertInto(QStringLiteral("viewMenu"), firstView, MenuNode::separator());
    insertAccessToolMenus(); // M86
}

void BaseHost::syncEnablement()
{
    const bool db = m_service != nullptr && m_service->hasDatabase();
    const bool writable = db && !m_service->readOnly();
    for (const Command &c : kCommands) {
        actions()->setEnabled(id(c.id), db);
    }
    for (const Command &c : kToggles) {
        actions()->setEnabled(id(c.id), db);
    }
    const DatasheetModel *sheet = m_service != nullptr ? m_service->datasheet() : nullptr;
    const bool datasheet = db && m_view == QLatin1String("datasheet") && sheet != nullptr && !sheet->sourceKind().isEmpty();
    const bool tableSheet = datasheet && sheet->sourceKind() == QLatin1String("table");
    const bool editable = tableSheet && sheet->editable();
    const bool form = db && m_view == QLatin1String("form");
    const bool selected = db && !m_selectedName.isEmpty();
    const bool dataObject = selected && (m_selectedKind == QLatin1String("table") || m_selectedKind == QLatin1String("query"));
    actions()->setEnabled({id("baseOpenObjectAction"), id("baseViewDesignAction"), id("baseRenameObjectAction"),
                           id("baseDeleteObjectAction")},
                          selected && writable);
    actions()->setEnabled(id("baseOpenObjectAction"), selected);
    actions()->setEnabled(id("baseViewSqlAction"),
                          db && (m_selectedKind == QLatin1String("query") || m_view == QLatin1String("queryDesign")));
    actions()->setEnabled({id("baseNewRecordAction"), id("baseDeleteRecordAction")}, (editable || form) && writable);
    actions()->setEnabled(id("baseSaveRecordAction"), (editable || form) && writable);
    actions()->setEnabled({id("baseSortAscendingAction"), id("baseSortDescendingAction"), id("baseTotalsRowAction")},
                          datasheet);
    actions()->setEnabled({id("baseFilterBySelectionAction"), id("baseFilterExcludingAction"), id("baseFilterAction"),
                           id("baseClearFiltersAction")},
                          tableSheet);
    actions()->setEnabled(id("baseRefreshAction"), datasheet || form || m_view == QLatin1String("report"));
    actions()->setEnabled({id("baseNewFormAction"), id("baseNewReportAction")}, dataObject && writable);
    actions()->setEnabled({id("baseExportCsvAction"), id("baseExportXlsxAction")}, datasheet || dataObject);
    actions()->setEnabled(id("baseExportPdfAction"), printable());
    for (const char *raw : {"baseNewTableAction", "baseTableDesignAction", "baseQueryDesignAction", "baseFormWizardAction",
                            "baseReportWizardAction", "baseImportCsvAction", "baseImportExcelAction",
                            "baseImportAccessAction", "baseCompactRepairAction", "baseDatabasePropertiesAction",
                            "baseNewMacroAction", "baseLabelsWizardAction"}) {
        actions()->setEnabled(id(raw), writable);
    }
    actions()->setEnabled(id("baseExportXmlAction"), datasheet || dataObject);
    const bool tableDesign = db && m_view == QLatin1String("tableDesign");
    for (const char *raw : {"baseDesignAddFieldAction", "baseDesignDeleteFieldAction", "baseDesignMoveUpAction",
                            "baseDesignMoveDownAction", "baseDesignPrimaryKeyAction", "baseDesignApplyAction",
                            "baseDesignDiscardAction"}) {
        actions()->setEnabled(id(raw), tableDesign && writable);
    }
    actions()->setEnabled(id("baseDesignTestRulesAction"), tableDesign && !m_name.isEmpty());
    const bool queryDesign = db && m_view == QLatin1String("queryDesign");
    for (const char *raw : {"baseQueryRunAction", "baseQueryAddTableAction", "baseQueryClearAction",
                            "baseQueryTotalsAction", "baseQueryParametersAction"}) {
        actions()->setEnabled(id(raw), queryDesign);
    }
    const bool macroDesign = db && m_view == QLatin1String("macroDesign");
    actions()->setEnabled(id("baseMacroRunAction"), macroDesign);
    for (const char *raw : {"baseMacroAddActionAction", "baseMacroDeleteActionAction", "baseMacroMoveUpAction",
                            "baseMacroMoveDownAction"}) {
        actions()->setEnabled(id(raw), macroDesign && writable);
    }
    for (const Command &c : kQueryTypes) {
        actions()->setEnabled(id(c.id), queryDesign && writable);
    }
    // File commands: a file field of a saved record in an editable table.
    const bool fileField = writable && !m_fileTarget.value(QStringLiteral("field")).toString().isEmpty()
                           && m_fileTarget.value(QStringLiteral("rowid")).toLongLong() > 0
                           && (tableSheet || form);
    const bool hasFile = fileField && m_fileTarget.value(QStringLiteral("hasFile")).toBool();
    actions()->setEnabled(id("baseFileInsertAction"), fileField);
    actions()->setEnabled({id("baseFileSaveAction"), id("baseFileRemoveAction")}, hasFile);
    actions()->setEnabled(id("baseFormPreviewAction"), form);
    actions()->setEnabled({id("baseFormAddSubformAction"), id("baseFormAddButtonAction")}, form && writable);
    actions()->setEnabled(id("baseReportRefreshAction"), db && m_view == QLatin1String("report"));
    actions()->setEnabled(id("baseReportGroupSortAction"), db && m_view == QLatin1String("report") && !m_report.isNull());
    // Undo/redo: the designer's history while one is active, else the database's.
    QUndoStack *stack = m_document->undoStack();
    actions()->setEnabled(id(ActionId::kEditUndo), m_designHistory ? m_designCanUndo : stack->canUndo());
    actions()->setEnabled(id(ActionId::kEditRedo), m_designHistory ? m_designCanRedo : stack->canRedo());
    if (ActionEntry *undo = action(id(ActionId::kEditUndo)); undo != nullptr && !m_designHistory) {
        undo->setText(stack->canUndo() ? tr("&Undo %1").arg(stack->undoText()) : tr("&Undo"));
    }
    actions()->setEnabled({id(ActionId::kEditCut), id(ActionId::kEditPaste)}, editable && writable);
    actions()->setEnabled({id(ActionId::kEditCopy), id(ActionId::kEditSelectAll), id(ActionId::kEditFind),
                           id(ActionId::kEditFindNext), id(ActionId::kEditFindPrevious)},
                          datasheet);
    actions()->setEnabled(id(ActionId::kEditReplace), false);
    const bool zoomable = db && m_view == QLatin1String("report");
    actions()->setEnabled({id(ActionId::kViewZoomIn), id(ActionId::kViewZoomOut), id(ActionId::kViewZoomReset)},
                          zoomable);
    actions()->setEnabled({id(ActionId::kFilePrint), id(ActionId::kFilePrintPreview)}, printable());
    syncAccessToolEnablement(writable); // M86
    updateStatus();
}

void BaseHost::setChecked(const QString &commandId, bool checked)
{
    if (ActionEntry *entry = action(commandId)) {
        entry->setChecked(checked);
    }
}

void BaseHost::updateStatus()
{
    const DatasheetModel *sheet = m_service != nullptr ? m_service->datasheet() : nullptr;
    if (m_view == QLatin1String("datasheet") && sheet != nullptr && !sheet->sourceKind().isEmpty()) {
        QString text = tr("%1 · %n record(s)", nullptr, sheet->recordCount()).arg(sheet->sourceName());
        if (!sheet->filters().isEmpty()) {
            text += tr(" · filtered");
        }
        if (sheet->truncated()) {
            text += tr(" · showing the first %1 of %2").arg(sheet->recordCount()).arg(sheet->totalRows());
        }
        setStatusInfo(text);
    } else if (!m_name.isEmpty()) {
        setStatusInfo(m_name);
    } else {
        setStatusInfo(m_service != nullptr ? m_service->databaseName() : QString());
    }
}

} // namespace QindaOffice::Apps::Base
