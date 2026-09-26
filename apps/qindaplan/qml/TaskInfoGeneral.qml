// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The General page of Task Information: name, duration, dates, progress,
// actuals, scheduling mode and resources, as the sheet's own text (so a
// value can be typed exactly as in the sheet).
Tk.Flex {
    id: page

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function load(info) {
        nameField.text = String(info.name || "")
        durationField.text = String(info.duration || "")
        startField.text = String(info.start || "")
        finishField.text = String(info.finish || "")
        percentField.text = String(info.percent || "0")
        actualStart.text = String(info.actualStart || "") === "NA" ? "" : String(info.actualStart || "")
        actualFinish.text = String(info.actualFinish || "") === "NA" ? "" : String(info.actualFinish || "")
        resourcesField.text = String(info.resources || "")
        modeBox.currentIndex = info.manual === true ? 1 : 0
        summary = info.summary === true
    }
    function collect() {
        return { name: nameField.text, duration: durationField.text, start: startField.text, finish: finishField.text,
                 percent: percentField.text, actualStart: actualStart.text.trim().length ? actualStart.text : "NA",
                 actualFinish: actualFinish.text.trim().length ? actualFinish.text : "NA",
                 resources: resourcesField.text, mode: modeBox.currentIndex === 1 ? "Manual" : "Auto" }
    }
    property bool summary: false

    Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "taskInfoName" } }
    Tk.Grid {
        columns: "1fr 1fr"
        columnGap: Tk.Theme.space.lg
        rowGap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Duration")
            enabled: !page.summary
            Tk.TextField { id: durationField; objectName: "taskInfoDuration"; placeholderText: qsTr("5d, 3 days, 4h") }
        }
        Tk.PropertyRow {
            label: qsTr("Schedule")
            enabled: !page.summary
            Tk.ComboBox { id: modeBox; objectName: "taskInfoMode"; small: true; model: [qsTr("Auto"), qsTr("Manual")] }
        }
        Tk.PropertyRow { label: qsTr("Start"); Tk.TextField { id: startField; objectName: "taskInfoStart" } }
        Tk.PropertyRow {
            label: qsTr("Finish")
            enabled: !page.summary
            Tk.TextField { id: finishField; objectName: "taskInfoFinish" }
        }
        Tk.PropertyRow {
            label: qsTr("% complete")
            enabled: !page.summary
            Tk.TextField { id: percentField; objectName: "taskInfoPercent"; inputMethodHints: Qt.ImhDigitsOnly }
        }
        Item { }
        Tk.PropertyRow {
            label: qsTr("Actual start")
            enabled: !page.summary
            Tk.TextField { id: actualStart; objectName: "taskInfoActualStart"; placeholderText: qsTr("NA") }
        }
        Tk.PropertyRow {
            label: qsTr("Actual finish")
            enabled: !page.summary
            Tk.TextField { id: actualFinish; objectName: "taskInfoActualFinish"; placeholderText: qsTr("NA") }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Resources")
        enabled: !page.summary
        hint: qsTr("Names with optional units: Ana[50%], Card stock[200]")
        Tk.TextField { id: resourcesField; objectName: "taskInfoResources" }
    }
}
