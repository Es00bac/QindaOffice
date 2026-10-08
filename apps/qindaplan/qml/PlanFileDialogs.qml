// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaOffice.Plan.App

// The file chooser behind File ▸ Import and Export: PDF of what is on
// screen (the Gantt chart or the open report), the task sheet as CSV or
// Excel (M63),
// Microsoft Project XML out and in, and the Timeline as a PNG. Results
// come back as toasts through PlanService.notify.
//
// AGENT-NOTE: ONE FileDialog that switches mode, declared as a window child
// like OfficeWindow's own dialogs (non-Item, so the window still seats one
// content Item and the dialog still finds its parent window).
Dialogs.FileDialog {
    id: files

    property PlanService plan: null
    property PlanHost host: null
    property string what: "pdf"   // pdf | csv | xlsx | mspdi | png | import

    objectName: "planFileDialog"
    title: what === "import" ? qsTr("Import Microsoft Project XML") : qsTr("Export")
    fileMode: what === "import" ? Dialogs.FileDialog.OpenFile : Dialogs.FileDialog.SaveFile
    nameFilters: what === "pdf" ? [qsTr("PDF (*.pdf)")]
               : what === "csv" ? [qsTr("CSV (*.csv)")]
               : what === "xlsx" ? [qsTr("Excel workbook (*.xlsx)")]
               : what === "png" ? [qsTr("PNG image (*.png)")] : [qsTr("Microsoft Project XML (*.xml)")]

    function request(kind) {
        what = kind
        if (kind !== "import") {
            const base = (plan !== null ? plan.title : "Project").replace(/[^A-Za-z0-9 _-]/g, "").trim() || "Project"
            const suffix = kind === "pdf" ? ".pdf" : kind === "csv" ? ".csv" : kind === "xlsx" ? ".xlsx"
                         : kind === "mspdi" ? ".xml" : ".png"
            selectedFile = "file://" + base + suffix
        }
        open()
    }
    function report(r) {
        if (r.ok) plan.notify(qsTr("Exported"), qsTr("Wrote %1").arg(String(r.path || "")), "info")
        else plan.notify(qsTr("Export failed"), String(r.error || ""), "danger")
    }
    onAccepted: {
        if (what === "import") {
            const r = plan.importMspdi(selectedFile)
            if (r.ok) plan.notify(qsTr("Imported"), qsTr("%1 tasks from Microsoft Project XML").arg(r.tasks), "info")
            else plan.notify(qsTr("Import failed"), String(r.error || ""), "danger")
        } else if (what === "pdf") {
            report(host.exportPdfTo(selectedFile))
        } else if (what === "csv") {
            report(plan.exportCsv(selectedFile))
        } else if (what === "xlsx") {
            report(plan.exportXlsx(selectedFile))
        } else if (what === "mspdi") {
            report(plan.exportMspdi(selectedFile))
        } else {
            report(plan.exportTimelinePng(selectedFile, 1600, 280))
        }
    }
}
