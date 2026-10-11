// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Dialog {
    id: dialog
    required property QtObject references
    property string oldTag: ""
    objectName: "sourcesDialog"
    title: qsTr("Manage Sources")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: ""
    secondaryText: qsTr("Close")
    function load(index) {
        const record = index >= 0 && index < references.sources.length ? references.sources[index] : ({ type: "Book", editable: true })
        oldTag = record.tag || ""
        form.editable = record.editable !== false
        form.load(record)
        references.clearError()
    }
    Connections {
        target: dialog.references
        function onDialogRequested(kind) {
            if (kind !== "sources") return
            source.currentIndex = dialog.references.sources.length ? 0 : -1
            dialog.load(source.currentIndex)
            style.currentIndex = dialog.references.style === "numeric" ? 1 : 0
            dialog.open()
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("Sources are stored in this document. Choose a source to edit, or create a new one."); width: parent.width; wrapMode: Text.Wrap }
        Tk.PropertyRow {
            label: qsTr("Source")
            Tk.ComboBox { id: source; objectName: "sourcePicker"; model: dialog.references.sources.map(function(s) { return s.tag + " — " + s.title }); onActivated: function(index) { dialog.load(index) } }
        }
        Tk.Flex {
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "sourceNew"; text: qsTr("New source"); iconName: "plus"; onClicked: { source.currentIndex = -1; dialog.load(-1) } }
            Tk.Button {
                objectName: "sourceDelete"; text: qsTr("Delete source"); enabled: dialog.oldTag.length > 0
                onClicked: if (dialog.references.deleteSource(dialog.oldTag)) { source.currentIndex = -1; dialog.load(-1) }
            }
        }
        SourceFields { id: form; objectName: "sourceFields"; width: parent.width }
        Tk.Caption { visible: !form.editable; text: qsTr("This imported source type is preserved. Its fields are read-only."); width: parent.width; wrapMode: Text.Wrap }
        Tk.Button {
            objectName: "sourceSave"; text: qsTr("Save source"); enabled: form.editable
            onClicked: if (dialog.references.saveSource(dialog.oldTag, form.answer())) { dialog.oldTag = form.answer().tag; source.currentIndex = dialog.references.sources.map(function(s) { return s.tag }).indexOf(dialog.oldTag) }
        }
        Tk.PropertyRow {
            label: qsTr("Citation style")
            Tk.ComboBox { id: style; objectName: "referenceStyle"; model: [qsTr("Author-date"), qsTr("Numeric")]; onActivated: function(index) { dialog.references.setStyle(index === 1 ? "numeric" : "authorDate") } }
        }
        Tk.Caption { visible: dialog.references.style === "preserved"; text: qsTr("The imported style is preserved. Choose a style above to reformat supported citations."); width: parent.width; wrapMode: Text.Wrap }
        Tk.Caption { objectName: "sourceError"; text: dialog.references.error; visible: text.length > 0; width: parent.width; wrapMode: Text.Wrap; color: Tk.Theme.color.danger }
    }
}
