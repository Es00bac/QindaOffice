// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Dialog {
    id: dialog
    required property QtObject references
    property string kind: "citation"
    property bool editing: false
    property var targets: []
    objectName: "referenceDialog"
    title: kind === "citation" ? (editing ? qsTr("Edit Citation") : qsTr("Insert Citation")) : kind === "caption" ? qsTr("Insert Caption") : kind === "toc" ? qsTr("Table of Contents") : qsTr("Cross-reference")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: ""
    secondaryText: qsTr("Cancel")
    function apply() {
        let ok = false
        if (kind === "citation") {
            const tag = source.currentIndex >= 0 ? references.sources[source.currentIndex].tag : ""
            ok = editing ? references.editCitation(tag, pages.text) : references.insertCitation(tag, pages.text)
        }
        else if (kind === "caption") ok = references.insertCaption(["Figure", "Table", "Equation"][label.currentIndex], caption.text)
        else if (kind === "crossReference") ok = references.insertCrossReference(target.currentIndex >= 0 ? targets[target.currentIndex].bookmark : "", ["text", "number", "page"][display.currentIndex], linked.checked)
        else ok = references.applyToc({ first: first.value, last: last.value, links: tocLinks.checked, leaders: leaders.checked })
        if (ok) close()
    }
    Connections {
        target: dialog.references
        function onDialogRequested(kind) {
            if (kind === "sources") return
            dialog.editing = kind === "editCitation"
            dialog.kind = dialog.editing ? "citation" : kind
            dialog.targets = dialog.references.targets()
            source.currentIndex = dialog.references.sources.length ? 0 : -1
            target.currentIndex = dialog.targets.length ? 0 : -1
            pages.text = ""; caption.text = ""
            if (dialog.editing) {
                const info = dialog.references.citationInfo()
                source.currentIndex = dialog.references.sources.map(function(s) { return s.tag }).indexOf(info.tag)
                pages.text = info.pages
            }
            const options = dialog.references.tocOptions()
            first.value = options.first; last.value = options.last
            tocLinks.checked = options.links; leaders.checked = options.leaders
            dialog.open()
        }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            visible: dialog.kind === "citation"
            label: qsTr("Source")
            Tk.ComboBox { id: source; objectName: "citationSource"; model: dialog.references.sources.map(function(s) { return s.tag + " — " + s.title }) }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "citation"
            label: qsTr("Pages")
            Tk.TextField { id: pages; objectName: "citationPages"; placeholderText: qsTr("Optional page or range") }
        }
        Tk.Button { visible: dialog.kind === "citation"; objectName: "citationManageSources"; text: qsTr("Manage sources…"); onClicked: { dialog.close(); dialog.references.request("sources") } }
        Tk.PropertyRow {
            visible: dialog.kind === "caption"
            label: qsTr("Label")
            Tk.ComboBox { id: label; objectName: "captionLabel"; model: [qsTr("Figure"), qsTr("Table"), qsTr("Equation")] }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "caption"
            label: qsTr("Caption")
            Tk.TextField { id: caption; objectName: "captionText"; placeholderText: qsTr("Describe the figure, table or equation") }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "crossReference"
            label: qsTr("Target")
            Tk.ComboBox { id: target; objectName: "referenceTarget"; model: dialog.targets.map(function(t) { return t.text || t.bookmark }) }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "crossReference"
            label: qsTr("Insert")
            Tk.ComboBox { id: display; objectName: "referenceDisplay"; model: [qsTr("Caption text"), qsTr("Number"), qsTr("Page")] }
        }
        Tk.CheckBox { id: linked; objectName: "referenceLink"; visible: dialog.kind === "crossReference"; text: qsTr("Insert as hyperlink"); checked: true }
        Tk.PropertyRow {
            visible: dialog.kind === "toc"
            label: qsTr("First level")
            Tk.NumberField { id: first; objectName: "tocFirst"; from: 1; to: 9; decimals: 0; onValueModified: function(v) { value = v } }
        }
        Tk.PropertyRow {
            visible: dialog.kind === "toc"
            label: qsTr("Last level")
            Tk.NumberField { id: last; objectName: "tocLast"; from: 1; to: 9; decimals: 0; onValueModified: function(v) { value = v } }
        }
        Tk.CheckBox { id: tocLinks; objectName: "tocLinks"; visible: dialog.kind === "toc"; text: qsTr("Link entries to headings") }
        Tk.CheckBox { id: leaders; objectName: "tocLeaders"; visible: dialog.kind === "toc"; text: qsTr("Dot leaders before page numbers") }
        Tk.Caption { visible: dialog.kind === "toc"; text: qsTr("Refresh explicitly after changing headings or page layout. Ctrl-click a linked entry to jump to its heading."); width: parent.width; wrapMode: Text.Wrap }
        Tk.Caption { objectName: "referenceError"; text: dialog.references.error; visible: text.length > 0; width: parent.width; wrapMode: Text.Wrap; color: Tk.Theme.color.danger }
        Tk.Button { objectName: "referenceApply"; text: dialog.kind === "toc" ? qsTr("Apply and refresh") : dialog.editing ? qsTr("Apply") : qsTr("Insert"); onClicked: dialog.apply() }
    }
}
