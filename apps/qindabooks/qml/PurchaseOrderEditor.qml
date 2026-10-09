// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Create or edit a purchase order (M68, D-077), as QuickBooks lays one out:
// vendor, date, P.O. number, expected date, ship-to, the lines (item or
// category, customer:job, quantity, rate, what bills received, closed by
// hand), the vendor message and an internal memo. An order never posts; it
// edits in place. Create Bill receives what is left; Closed closes the
// whole order. Totals come from BooksService.computeTotals.
Tk.Dialog {
    id: editor

    property BooksService books: null
    property BooksHost host: null
    property var doc: ({})
    property var totals: ({})
    property var vendors: []
    readonly property bool existing: Number(doc.id || 0) > 0
    readonly property bool voided: doc.status === "void"
    signal billRequested(int orderId)

    objectName: "purchaseOrderEditor"
    title: existing ? qsTr("Purchase order %1").arg(doc.number || "") : qsTr("New purchase order")
    subtitle: existing ? String(doc.statusLabel || "") : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 2.4
    primaryText: voided ? qsTr("Close") : qsTr("Save and close")
    secondaryText: voided ? "" : qsTr("Cancel")
    primaryEnabled: voided || (vendorBox.currentIndex > 0 && (doc.lines || []).length > 0)

    function openFor(id) {
        vendors = [{ id: 0, name: qsTr("Choose a vendor") }].concat(books.parties("vendor", false))
        if (Number(id || 0) > 0) {
            doc = books.document(id)
        } else {
            const s = books.settings
            doc = { kind: "purchaseOrder", date: books.today, dueDate: "", notes: "", memo: "",
                    shipTo: [String(s.name || ""), String(s.address || "")].filter(function(t) { return t.length > 0 }).join("\n"),
                    lines: [{ itemId: 0, accountId: 0, description: "", quantity: "1", rate: "", customerId: 0 }] }
        }
        error.visible = false
        recompute()
        open()
    }
    function reload() { if (existing) { doc = books.document(doc.id); recompute() } }
    function edit(field, value) { const next = Object.assign({}, doc); next[field] = value; doc = next; recompute() }
    function recompute() { totals = books !== null ? books.computeTotals(Object.assign({}, doc, { kind: "purchaseOrder" })) : ({}) }
    function save() {
        const result = books.saveDocument(Object.assign({}, doc, { kind: "purchaseOrder", state: "open" }))
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; open(); return false }
        host.setPrintSource("document", result.id, ({}))
        return true
    }
    function fail(result) { if (!result.ok) { error.text = String(result.error || ""); error.visible = true } return result.ok }
    onAccepted: if (!voided) save()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Notice { visible: editor.voided; variant: "warning"; title: qsTr("Voided"); text: qsTr("This purchase order was voided; it stays in the records.") }

        Tk.Grid {
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Vendor")
                Tk.ComboBox {
                    id: vendorBox
                    objectName: "orderVendor"
                    small: true
                    // The vendor stays once a bill has received the order (the engine refuses it too).
                    enabled: !editor.voided && editor.doc.status !== "partiallyReceived" && editor.doc.status !== "closed"
                    textRole: "name"
                    model: editor.vendors
                    currentIndex: Math.max(0, editor.vendors.map(function(v) { return v.id }).indexOf(editor.doc.partyId || 0))
                    onActivated: function(i) { editor.edit("partyId", editor.vendors[i].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Date")
                Tk.TextField { objectName: "orderDate"; small: true; mono: true; readOnly: editor.voided; text: String(editor.doc.date || ""); onEditingFinished: editor.edit("date", text) }
            }
            Tk.PropertyRow {
                label: qsTr("P.O. no.")
                Tk.TextField { objectName: "orderNumber"; small: true; mono: true; readOnly: editor.voided; placeholderText: qsTr("Automatic"); text: String(editor.doc.number || ""); onEditingFinished: editor.edit("number", text) }
            }
            Tk.PropertyRow {
                label: qsTr("Expected")
                Tk.TextField { objectName: "orderExpected"; small: true; mono: true; readOnly: editor.voided; placeholderText: "yyyy-mm-dd"; text: String(editor.doc.dueDate || ""); onEditingFinished: editor.edit("dueDate", text) }
            }
            Tk.PropertyRow {
                label: qsTr("Status")
                Tk.Label { objectName: "orderStatus"; text: String(editor.doc.statusLabel || qsTr("New")) }
            }
            Tk.PropertyRow {
                label: qsTr("Closed")
                Tk.Switch {
                    objectName: "orderClosed"
                    enabled: editor.existing && !editor.voided
                    checked: editor.doc.closedByHand === true || editor.doc.status === "closed"
                    tooltip: qsTr("Close the order: nothing more is expected from the vendor")
                    onToggled: {
                        if (editor.fail(editor.books.setOrderClosed(editor.doc.id, checked))) editor.reload()
                        checked = Qt.binding(function() { return editor.doc.closedByHand === true || editor.doc.status === "closed" })
                    }
                }
            }
        }

        // Narrower than the lines need, they scroll sideways (520 px windows).
        Tk.Scroll {
            id: linesScroll
            objectName: "orderLinesScroll"
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Hidden
            // Room under the lines for the overlay scrollbar.
            implicitHeight: orderLines.implicitHeight + Tk.Theme.space.md
            // AGENT-NOTE: a plain Item carries the minimum width — a Flex sets
            // its own implicit width — and Tk.Scroll stretches it to a wider viewport.
            Item {
                implicitWidth: Tk.Theme.size.dialogWidth * 2.1
                implicitHeight: orderLines.implicitHeight
                OrderLinesTable {
                    id: orderLines
                    objectName: "orderLines"
                    anchors.fill: parent
                    lines: editor.doc.lines || []
                    // revision: new items, categories and customers show at once.
                    items: editor.books !== null && editor.books.revision >= 0 ? editor.books.items(false) : []
                    accounts: editor.books !== null && editor.books.revision >= 0 ? editor.books.expenseAccounts() : []
                    customers: editor.books !== null && editor.books.revision >= 0 ? editor.books.parties("customer", false) : []
                    readOnly: editor.voided
                    amounts: (editor.totals.lines || []).map(function(l) { return l.amountText })
                    onEdited: function(lines) { editor.edit("lines", lines) }
                    onCloseLine: function(lineId, closed) { if (editor.fail(editor.books.setOrderLineClosed(lineId, closed))) editor.reload() }
                }
            }
        }

        Tk.Grid {
            columns: "2fr 2fr 2fr"
            columnGap: Tk.Theme.space.lg
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("Ship to") }
                Tk.TextArea { objectName: "orderShipTo"; rows: 3; readOnly: editor.voided; placeholderText: qsTr("Where the vendor delivers"); text: String(editor.doc.shipTo || ""); onEditingFinished: editor.edit("shipTo", text) }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("Vendor message") }
                Tk.TextArea { objectName: "orderMessage"; rows: 2; readOnly: editor.voided; placeholderText: qsTr("Printed on the order"); text: String(editor.doc.notes || ""); onEditingFinished: editor.edit("notes", text) }
                Tk.TextArea { objectName: "orderMemo"; rows: 1; readOnly: editor.voided; placeholderText: qsTr("Internal memo (not printed)"); text: String(editor.doc.memo || ""); onEditingFinished: editor.edit("memo", text) }
            }
            Tk.Box {
                objectName: "orderTotals"
                color: Tk.Theme.color.panelAlt
                radius: Tk.Theme.radius.md
                padding: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Tk.KeyValue { key: qsTr("Total"); value: String(editor.totals.totalText || ""); accent: true }
                    Tk.KeyValue { visible: editor.existing; key: qsTr("Still to receive"); value: String(editor.doc.balanceText || "") }
                }
            }
        }

        Tk.Flex {
            objectName: "orderActions"
            visible: editor.existing
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { small: true; variant: "ghost"; iconName: "printer"; text: qsTr("Print"); onClicked: { editor.host.setPrintSource("document", editor.doc.id, ({})); editor.host.actions.trigger("filePrintAction") } }
            Tk.Button { small: true; variant: "ghost"; iconName: "file-down"; text: qsTr("Save PDF…"); onClicked: pdfDialog.open() }
            Tk.Button { small: true; variant: "ghost"; iconName: "send"; text: qsTr("Email"); onClicked: editor.books.emailDocument(editor.doc.id) }
            Tk.Button {
                objectName: "orderCreateBill"
                small: true; variant: "ghost"; iconName: "clipboard-check"; text: qsTr("Create bill")
                visible: editor.doc.status === "open" || editor.doc.status === "partiallyReceived"
                onClicked: { const id = editor.doc.id; editor.close(); editor.billRequested(id) }
            }
            Tk.Spacer { }
            Tk.Button { small: true; variant: "ghost"; iconName: "circle-x"; text: qsTr("Void…"); visible: !editor.voided; onClicked: voidPrompt.open() }
        }
    }
    Dialogs.FileDialog {
        id: pdfDialog
        title: qsTr("Save as PDF")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: editor.books.exportDocumentPdf(editor.doc.id, selectedFile)
    }
    Tk.PromptDialog {
        id: voidPrompt
        title: qsTr("Void this purchase order")
        label: qsTr("Why? The reason is kept in the audit trail.")
        primaryText: qsTr("Void")
        destructive: true
        onAccepted: { if (editor.fail(editor.books.voidDocument(editor.doc.id, text))) editor.close() }
    }
}
