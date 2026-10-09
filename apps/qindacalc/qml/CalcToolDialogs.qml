// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// The M78 dialogs (D-087) and the host requests that open them: Page Setup,
// Goal Seek and its status, the Scenario Manager with Add/Edit Scenario and
// Scenario Summary, Data Table, Text to Columns (and its overwrite
// question), Remove Duplicates (and its report), Consolidate, New/Edit
// Formatting Rule, and a validation Warning / Information question; and
// M82's (D-091) Custom Views with its Add View.
// AGENT-GUARD: a QtObject, not an Item — the window seats ONE content child
// (Tk.AppWindow); the dialogs reach the window's overlay through hostItem.
QtObject {
    id: root

    required property CalcHost host
    property Item hostItem: null
    // The Rules Manager, refreshed after a rule is saved.
    property var manager: null

    function openRule(index) {
        root.rule.load(index)
        root.rule.open()
    }

    property PageSetupDialog pageSetup: PageSetupDialog { host: root.host; hostItem: root.hostItem }
    property GoalSeekDialog goalSeek: GoalSeekDialog {
        host: root.host
        hostItem: root.hostItem
        onFinished: function(result) {
            root.goalSeekStatus.outcome = result
            root.goalSeekStatus.open()
        }
    }
    property GoalSeekStatusDialog goalSeekStatus: GoalSeekStatusDialog { host: root.host; hostItem: root.hostItem }
    property ScenarioManagerDialog scenarios: ScenarioManagerDialog {
        host: root.host
        hostItem: root.hostItem
        onEditRequested: function(index) {
            root.scenarioEdit.load(index)
            root.scenarioEdit.open()
        }
        onSummaryRequested: {
            root.scenarioSummary.text = ""
            root.scenarioSummary.open()
        }
    }
    property ScenarioEditDialog scenarioEdit: ScenarioEditDialog {
        host: root.host
        hostItem: root.hostItem
        onSaved: root.scenarios.load()
    }
    property Tk.PromptDialog scenarioSummary: Tk.PromptDialog {
        objectName: "scenarioSummaryDialog"
        hostItem: root.hostItem
        title: qsTr("Scenario Summary")
        label: qsTr("Result cells")
        placeholderText: qsTr("$B$5,$B$6")
        acceptEmpty: true
        onAccepted: {
            const problem = root.host.scenarioSummaryFor(text)
            if (problem.length > 0) {
                root.scenarios.note = problem
            } else {
                root.scenarios.close()
            }
        }
    }
    property DataTableDialog dataTable: DataTableDialog { host: root.host; hostItem: root.hostItem }
    property TextToColumnsDialog textToColumns: TextToColumnsDialog {
        host: root.host
        hostItem: root.hostItem
        onOverwriteAsked: root.overwrite.open()
    }
    property Tk.MessageDialog overwrite: Tk.MessageDialog {
        objectName: "textToColumnsOverwrite"
        hostItem: root.hostItem
        title: qsTr("Text to Columns")
        text: qsTr("Do you want to replace the contents of the destination cells?")
        primaryText: qsTr("OK")
        onAccepted: root.textToColumns.finish(true)
    }
    property RemoveDuplicatesDialog removeDuplicates: RemoveDuplicatesDialog {
        host: root.host
        hostItem: root.hostItem
        onReported: function(message) {
            root.report.text = message
            root.report.open()
        }
    }
    property Tk.MessageDialog report: Tk.MessageDialog {
        objectName: "removeDuplicatesReport"
        hostItem: root.hostItem
        title: qsTr("Remove Duplicates")
        secondaryText: ""
    }
    property ConsolidateDialog consolidate: ConsolidateDialog { host: root.host; hostItem: root.hostItem }
    property CustomViewsDialog customViews: CustomViewsDialog {
        host: root.host
        hostItem: root.hostItem
        onAddRequested: {
            root.customViewAdd.load()
            root.customViewAdd.open()
        }
    }
    property CustomViewAddDialog customViewAdd: CustomViewAddDialog {
        host: root.host
        hostItem: root.hostItem
        onSaved: root.customViews.close() // Excel's Add closes Custom Views too
    }
    property CondFormatRuleDialog rule: CondFormatRuleDialog {
        host: root.host
        hostItem: root.hostItem
        onSaved: if (root.manager !== null && root.manager.visible) root.manager.load()
    }
    // A Warning rule asks "Continue?"; an Information rule just tells.
    property Tk.MessageDialog question: Tk.MessageDialog {
        property bool warning: true
        objectName: "validationQuestion"
        hostItem: root.hostItem
        variant: warning ? "warning" : "info"
        primaryText: warning ? qsTr("Yes") : qsTr("OK")
        secondaryText: warning ? qsTr("No") : qsTr("Cancel")
        onAccepted: root.host.answerQuestionedInput(true)
        onRejected: root.host.answerQuestionedInput(false)
    }

    property Connections requests: Connections {
        target: root.host
        function onPageSetupRequested(tab) { root.pageSetup.load(tab); root.pageSetup.open() }
        function onGoalSeekRequested() { root.goalSeek.load(); root.goalSeek.open() }
        function onScenarioManagerRequested() { root.scenarios.load(); root.scenarios.open() }
        function onDataTableRequested(selection) { root.dataTable.load(selection); root.dataTable.open() }
        function onTextToColumnsRequested() { root.textToColumns.load(); root.textToColumns.open() }
        function onRemoveDuplicatesRequested() { root.removeDuplicates.load(); root.removeDuplicates.open() }
        function onConsolidateRequested() { root.consolidate.load(); root.consolidate.open() }
        function onCustomViewsRequested() { root.customViews.load(); root.customViews.open() }
        function onCondFormatRuleRequested(index) { root.openRule(index) }
        function onInputQuestioned(title, message, warning) {
            root.question.warning = warning
            root.question.title = title.length > 0 ? title : qsTr("QindaCalc")
            root.question.text = message + (warning ? "\n\n" + qsTr("Continue?") : "")
            root.question.open()
        }
    }
}
