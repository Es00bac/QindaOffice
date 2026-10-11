// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaBase's ribbon (D-041, D-049): File · Home (views, clipboard,
// records, sort & filter, find, file field, hyperlink) · Create · External
// Data · Database Tools · View, plus the contextual Table Design, Query
// Design, Form & Report Design and (M80) Macro Design tabs the host offers
// while a designer is open (BaseHost ribbonContexts). Every string is an
// ActionEntry id BaseHost registers (M86's: base_host_actions_access.cpp).
QtObject {
    readonly property var tabs: [
        StandardRibbon.fileTab([
            { id: "database", label: qsTr("Database"), items: [
                { action: "baseBackupAction", size: "large" },
                "baseDatabasePropertiesAction", "baseCompactRepairAction"] }],
            [], [{ action: "baseExportPdfAction" }]),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            { id: "views", label: qsTr("Views"), items: [
                { action: "baseOpenObjectAction", size: "large" },
                "baseViewDesignAction", "baseViewSqlAction"] },
            StandardRibbon.clipboardGroup(),
            StandardRibbon.historyGroup(),
            { id: "records", label: qsTr("Records"), items: [
                { action: "baseNewRecordAction", size: "large" },
                "baseSaveRecordAction", "baseDeleteRecordAction", "baseRefreshAction", "baseTotalsRowAction"] },
            { id: "sortFilter", label: qsTr("Sort & filter"), items: [
                { action: "baseFilterBySelectionAction", size: "large" },
                "baseSortAscendingAction", "baseSortDescendingAction", "baseFilterExcludingAction",
                "baseFilterAction", "baseClearFiltersAction"] },
            { id: "find", label: qsTr("Find"), items: [
                { action: "editFindAction", size: "large" },
                { action: "editSelectAllAction", iconName: "text-select" }] },
            { id: "fileField", label: qsTr("File field"), items: [
                { action: "baseFileInsertAction", size: "large" },
                "baseFileSaveAction", "baseFileRemoveAction"] },
            { id: "hyperlink", label: qsTr("Hyperlink"), items: [
                { action: "baseEditHyperlinkAction", size: "large" }, "baseOpenHyperlinkAction"] },
            { id: "object", label: qsTr("Object"), items: [
                "baseRenameObjectAction", "baseDeleteObjectAction"] }
        ] },
        { id: "create", label: qsTr("Create"), keyTip: "C", groups: [
            { id: "tables", label: qsTr("Tables"), items: [
                { action: "baseNewTableAction", size: "large" }, "baseTableDesignAction"] },
            { id: "queries", label: qsTr("Queries"), items: [
                { action: "baseQueryDesignAction", size: "large" },
                "baseSimpleQueryWizardAction", "baseCrosstabWizardAction", "baseFindDuplicatesWizardAction",
                "baseFindUnmatchedWizardAction"] },
            { id: "forms", label: qsTr("Forms"), items: [
                { action: "baseNewFormAction", size: "large" }, "baseFormWizardAction"] },
            { id: "reports", label: qsTr("Reports"), items: [
                { action: "baseNewReportAction", size: "large" }, "baseReportWizardAction", "baseLabelsWizardAction"] },
            { id: "macros", label: qsTr("Macros"), items: [{ action: "baseNewMacroAction", size: "large" }] }
        ] },
        { id: "external", label: qsTr("External Data"), keyTip: "X", groups: [
            { id: "import", label: qsTr("Import"), items: [
                { action: "baseImportCsvAction", size: "large" }, "baseImportExcelAction", "baseImportAccessAction",
                "baseImportXmlAction"] },
            { id: "link", label: qsTr("Link"), items: [
                { action: "baseLinkTableAction", size: "large" }, "baseLinkedTableManagerAction", "baseRefreshLinksAction"] },
            { id: "export", label: qsTr("Export"), items: [
                { action: "baseExportCsvAction", size: "large" }, "baseExportXlsxAction", "baseExportXmlAction"] }
        ] },
        { id: "tools", label: qsTr("Database Tools"), keyTip: "Y", groups: [
            { id: "relationships", label: qsTr("Relationships"), items: [
                { action: "baseRelationshipsAction", size: "large" }] },
            { id: "sql", label: qsTr("SQL"), items: [{ action: "baseSqlConsoleAction", size: "large" }] },
            { id: "maintain", label: qsTr("Maintain"), items: [
                { action: "baseCheckIntegrityAction", size: "large" }] },
            { id: "logs", label: qsTr("Logs"), items: [{ action: "baseApplicationLogAction", size: "large" }] }
        ] },
        StandardRibbon.viewTab([
            { id: "panes", label: qsTr("Panes"), items: [{ action: "baseShowNavigationAction", size: "large" }] },
            StandardRibbon.assistantGroup([])
        ]),
        { id: "tableDesign", label: qsTr("Table Design"), contextId: "tableDesign", keyTip: "J", groups: [
            { id: "fields", label: qsTr("Fields"), items: [
                { action: "baseDesignAddFieldAction", size: "large" },
                "baseDesignDeleteFieldAction", "baseDesignMoveUpAction", "baseDesignMoveDownAction",
                "baseDesignPrimaryKeyAction"] },
            { id: "designChanges", label: qsTr("Changes"), items: [
                { action: "baseDesignApplyAction", size: "large" }, "baseDesignDiscardAction"] },
            { id: "designValidation", label: qsTr("Validation"), items: [
                { action: "baseDesignTestRulesAction", size: "large" }] },
            { id: "designIndexes", label: qsTr("Show/hide"), items: [{ action: "baseDesignIndexesAction", size: "large" }] },
            { id: "designEvents", label: qsTr("Table events"), items: [
                { action: "baseDataMacroAfterInsertAction", size: "large" },
                "baseDataMacroAfterUpdateAction", "baseDataMacroAfterDeleteAction",
                "baseDataMacroBeforeChangeAction", "baseDataMacroBeforeDeleteAction"] }
        ] },
        { id: "queryDesign", label: qsTr("Query Design"), contextId: "queryDesign", keyTip: "Q", groups: [
            { id: "results", label: qsTr("Results"), items: [{ action: "baseQueryRunAction", size: "large" }] },
            { id: "queryType", label: qsTr("Query type"), items: [
                { action: "baseQueryTypeSelectAction", size: "large" },
                "baseQueryTypeMakeTableAction", "baseQueryTypeAppendAction", "baseQueryTypeUpdateAction",
                "baseQueryTypeCrosstabAction", "baseQueryTypeDeleteAction", "baseQueryTypeUnionAction"] },
            { id: "querySetup", label: qsTr("Setup"), items: [
                { action: "baseQueryAddTableAction", size: "large" },
                "baseQueryTotalsAction", "baseQueryParametersAction", "baseQueryClearAction"] }
        ] },
        { id: "formDesign", label: qsTr("Form & Report"), contextId: "formDesign", keyTip: "D", groups: [
            { id: "formRecords", label: qsTr("Records"), items: [
                { action: "baseNewRecordAction", size: "large" }, "baseSaveRecordAction", "baseDeleteRecordAction"] },
            { id: "layout", label: qsTr("Layout"), items: [
                { action: "baseFormPreviewAction", size: "large" }, "baseReportRefreshAction", "baseReportGroupSortAction"] },
            { id: "controls", label: qsTr("Controls"), items: [
                { action: "baseFormAddSubformAction", size: "large" }, "baseFormAddButtonAction", "baseFormEventsAction"] },
            { id: "output", label: qsTr("Output"), items: [
                { action: "filePrintAction", size: "large" },
                { action: "filePrintPreviewAction", iconName: "eye" }, "baseExportPdfAction"] }
        ] },
        { id: "macroDesign", label: qsTr("Macro Design"), contextId: "macroDesign", keyTip: "M", groups: [
            { id: "macroTools", label: qsTr("Tools"), items: [{ action: "baseMacroRunAction", size: "large" }] },
            { id: "macroActions", label: qsTr("Actions"), items: [
                { action: "baseMacroAddActionAction", size: "large" },
                "baseMacroDeleteActionAction", "baseMacroMoveUpAction", "baseMacroMoveDownAction"] }
        ] }
    ]
}
