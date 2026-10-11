// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Flex {
    id: form
    property var values: ({})
    property bool editable: true
    property bool details: false
    readonly property var types: ["Book", "JournalArticle", "InternetSite"]
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    function answer() {
        const result = Object.assign({}, values)
        delete result.editable
        result.type = types[type.currentIndex]
        result.corporate = corporate.checked
        return result
    }
    function load(record) {
        values = Object.assign({}, record)
        type.currentIndex = Math.max(0, types.indexOf(record.type))
        corporate.checked = !!record.corporate
        details = false
    }
    Tk.PropertyRow {
        label: qsTr("Source type")
        Tk.ComboBox { id: type; objectName: "sourceType"; enabled: form.editable; model: [qsTr("Book"), qsTr("Journal article"), qsTr("Website")] }
    }
    Repeater {
        model: [{ key: "tag", label: qsTr("Tag"), hint: qsTr("Unique short name") },
                { key: "title", label: qsTr("Title"), hint: "" },
                { key: "author", label: qsTr("Author"), hint: qsTr("Last, First; Last, First") },
                { key: "year", label: qsTr("Year"), hint: "" }]
        Tk.PropertyRow {
            required property var modelData
            label: modelData.label
            Tk.TextField {
                objectName: "source_" + modelData.key
                enabled: form.editable
                text: form.values[modelData.key] || ""
                placeholderText: modelData.hint
                onTextEdited: { const copy = Object.assign({}, form.values); copy[modelData.key] = text; form.values = copy }
            }
        }
    }
    Tk.CheckBox { id: corporate; objectName: "sourceCorporate"; text: qsTr("Corporate author"); enabled: form.editable }
    Tk.Button { objectName: "sourceDetails"; text: form.details ? qsTr("Hide publication details") : qsTr("Publication details…"); onClicked: form.details = !form.details }
    Tk.Flex {
        visible: form.details
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: [{ key: "publisher", label: qsTr("Publisher") }, { key: "city", label: qsTr("City") },
                    { key: "journal", label: qsTr("Journal") }, { key: "volume", label: qsTr("Volume") },
                    { key: "issue", label: qsTr("Issue") }, { key: "pages", label: qsTr("Pages") },
                    { key: "site", label: qsTr("Website name") }, { key: "url", label: qsTr("URL") },
                    { key: "accessed", label: qsTr("Accessed") }]
            Tk.PropertyRow {
                required property var modelData
                label: modelData.label
                Tk.TextField {
                    objectName: "source_" + modelData.key
                    enabled: form.editable
                    text: form.values[modelData.key] || ""
                    placeholderText: modelData.key === "accessed" ? "YYYY-MM-DD" : ""
                    onTextEdited: { const copy = Object.assign({}, form.values); copy[modelData.key] = text; form.values = copy }
                }
            }
        }
    }
}
