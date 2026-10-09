// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The Label Wizard (M80, D-089 §7): mailing labels from a table or a
// query on Avery's sheets. Each line is written the way Access's prototype
// label shows it — {name}, or {city}, {region} {postal_code} — and saved as
// an Access expression ([city] & ", " & …); a line that starts with = is an
// expression as it is. Lines that come out empty close up. The first
// labels preview as they are typed; the report opens when it is made.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property var sources: []      // [{kind, name, text}]
    property int source: 0
    property var fields: []
    property var lines: ["", "", "", ""]
    property string productId: "Avery US Letter|5160"
    property string sortField: ""
    property bool sortDescending: false
    property int skip: 0
    property int copies: 1
    property real fontSize: 10
    property string reportName: ""
    property var preview: ({})

    title: qsTr("Label Wizard")
    subtitle: qsTr("Mailing labels on Avery sheets, one record to a label.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: qsTr("Make labels")
    primaryEnabled: sources.length > 0 && lines.some(function(l) { return l.trim().length > 0 })

    // {city}, {region} {postal_code} → [city] & ", " & [region] & " " & [postal_code]
    function expression(line) {
        const text = line.trim()
        if (text.length === 0 || text.charAt(0) === "=") return text
        const parts = []
        const re = /\{([^}]+)\}/g
        let at = 0
        let m
        while ((m = re.exec(text)) !== null) {
            if (m.index > at) parts.push('"' + text.slice(at, m.index).replace(/"/g, '""') + '"')
            parts.push("[" + m[1].trim() + "]")
            at = re.lastIndex
        }
        if (at < text.length) parts.push('"' + text.slice(at).replace(/"/g, '""') + '"')
        return parts.join(" & ")
    }
    function spec() {
        const s = sources[source] || ({ kind: "table", name: "" })
        return { source: { kind: s.kind, name: s.name }, product: productId,
                 lines: lines.map(expression).filter(function(l) { return l.length > 0 }),
                 sort: sortField ? [{ field: sortField, descending: sortDescending }] : [],
                 skip: skip, copies: copies, fontSize: fontSize }
    }
    function setLine(index, text) { const next = lines.slice(); next[index] = text; lines = next; refresh.restart() }
    function chooseSource(index) {
        source = index
        const s = sources[index] || ({ kind: "table", name: "" })
        fields = s.name ? base.sourceFields(s.kind, s.name).map(function(f) { return f.name }) : []
        sortField = ""
        reportName = s.name ? qsTr("Labels %1").arg(s.name) : ""
        // Start the lines from the fields an address uses, when they are there.
        const pick = function(names) { return fields.find(function(f) { return names.indexOf(f.toLowerCase()) >= 0 }) || "" }
        const name = pick(["name", "full_name", "company"])
        const street = pick(["address", "street", "address1"])
        const city = pick(["city", "town"])
        const region = pick(["region", "state", "province"])
        const postal = pick(["postal_code", "zip", "postal", "zip_code"])
        lines = [name ? "{" + name + "}" : "", street ? "{" + street + "}" : "",
                 city ? "{" + city + "}" + (region ? ", {" + region + "}" : "") + (postal ? " {" + postal + "}" : "") : "", ""]
        refresh.restart()
    }
    function openFor(kind, name) {
        const list = base.tableNames.map(function(t) { return { kind: "table", name: t, text: qsTr("Table · %1").arg(t) } })
            .concat(base.queryNames.map(function(q) { return { kind: "query", name: q, text: qsTr("Query · %1").arg(q) } }))
        sources = list
        const found = list.findIndex(function(s) { return s.kind === kind && s.name === name })
        productId = "Avery US Letter|5160"
        skip = 0
        copies = 1
        fontSize = 10
        chooseSource(Math.max(0, found))
        open()
    }
    function make() {
        let r = base.createLabels(reportName, spec())
        if (r.needsSystemTables) {
            base.addSystemTables()
            r = base.createLabels(reportName, spec())
        }
        if (!r.ok) { window.notify(qsTr("No labels made"), r.error, "danger"); return }
        window.baseHost.selectedKind = "report"
        window.baseHost.selectedName = r.name
        window.commands.openObject("report", r.name)
    }
    onAccepted: make()

    Timer {
        id: refresh
        interval: Tk.Theme.motion.slow
        onTriggered: dialog.preview = dialog.base !== null ? dialog.base.previewLabels(dialog.spec()) : ({})
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Records from")
            Tk.ComboBox {
                objectName: "labelSource"
                textRole: "text"
                model: dialog.sources
                currentIndex: dialog.source
                onActivated: function(i) { dialog.chooseSource(i) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Labels")
            Tk.ComboBox {
                objectName: "labelProduct"
                textRole: "label"
                model: dialog.base !== null ? dialog.base.labelProducts : []
                currentIndex: Math.max(0, model.findIndex(function(p) { return p.id === dialog.productId }))
                onActivated: function(i) { dialog.productId = model[i].id; refresh.restart() }
            }
        }
        Tk.Overline { title: qsTr("THE LABEL — {FIELD} FOR A FIELD; A LINE STARTING WITH = IS AN EXPRESSION") }
        Repeater {
            model: dialog.lines.length
            delegate: Tk.Flex {
                id: lineRow
                required property int index
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.TextField {
                    objectName: "labelLine_" + lineRow.index
                    mono: true
                    text: dialog.lines[lineRow.index]
                    placeholderText: lineRow.index === 0 ? "{name}" : ""
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onEditingFinished: if (text !== dialog.lines[lineRow.index]) dialog.setLine(lineRow.index, text)
                }
                Tk.ComboBox {
                    small: true
                    model: [qsTr("+ field")].concat(dialog.fields)
                    currentIndex: 0
                    tooltip: qsTr("Put a field at the end of this line")
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.7
                    onActivated: function(i) {
                        if (i === 0) return
                        const now = dialog.lines[lineRow.index]
                        dialog.setLine(lineRow.index, now + (now.length > 0 ? " " : "") + "{" + dialog.fields[i - 1] + "}")
                        currentIndex = 0
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Sort by"); Tk.Flex.shrink: 0 }
            Tk.ComboBox {
                objectName: "labelSort"
                small: true
                model: [qsTr("(as stored)")].concat(dialog.fields)
                currentIndex: Math.max(0, model.indexOf(dialog.sortField))
                onActivated: function(i) { dialog.sortField = i === 0 ? "" : dialog.fields[i - 1]; refresh.restart() }
            }
            Tk.CheckBox { text: qsTr("Z → A"); checked: dialog.sortDescending; onToggled: { dialog.sortDescending = checked; refresh.restart() } }
            Tk.Caption { text: qsTr("Text size"); Tk.Flex.shrink: 0 }
            Tk.NumberField { objectName: "labelFontSize"; from: 6; to: 24; value: dialog.fontSize; onValueModified: dialog.fontSize = value }
            Tk.Caption { text: qsTr("Skip"); Tk.Flex.shrink: 0 }
            Tk.NumberField {
                objectName: "labelSkip"; from: 0; to: 80; value: dialog.skip
                tooltip: qsTr("Labels already used on the first sheet")
                onValueModified: dialog.skip = value
            }
            Tk.Caption { text: qsTr("Copies"); Tk.Flex.shrink: 0 }
            Tk.NumberField { objectName: "labelCopies"; from: 1; to: 20; value: dialog.copies; onValueModified: dialog.copies = value }
        }
        Tk.PropertyRow {
            label: qsTr("Report name")
            Tk.TextField { objectName: "labelName"; text: dialog.reportName; onTextChanged: dialog.reportName = text }
        }
        Tk.Notice {
            visible: dialog.preview.ok === false && !!dialog.preview.error
            variant: dialog.preview.needsParameters ? "info" : "warning"
            text: dialog.preview.error || ""
        }
        Tk.Flex {
            objectName: "labelPreview"
            visible: (dialog.preview.labels || []).length > 0
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Repeater {
                model: (dialog.preview.labels || []).slice(0, 3)
                delegate: Tk.Card {
                    required property string modelData
                    padding: Tk.Theme.space.sm
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    Tk.Label { text: modelData; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
