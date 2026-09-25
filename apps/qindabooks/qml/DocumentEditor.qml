// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Create or edit one business document (D-047): invoice, estimate, sales
// receipt, credit memo, bill, expense, check or vendor credit. The kind
// decides the party (customer or vendor), whether lines are products or
// category accounts, and the dates shown. Totals come from
// BooksService.computeTotals on every edit — the editor never computes
// money. A posted document is edited through the service's update path
// (void and repost with an audit reason); a voided one is read-only.
Tk.Dialog {
    id: editor

    property BooksService books: null
    property BooksHost host: null
    property string kind: "invoice"
    property var doc: ({})
    property var totals: ({})
    readonly property bool existing: Number(doc.id || 0) > 0
    readonly property bool voided: doc.status === "void"
    readonly property bool sales: ["invoice", "estimate", "salesReceipt", "creditMemo"].indexOf(kind) >= 0
    readonly property bool paidNow: ["salesReceipt", "expense", "check"].indexOf(kind) >= 0
    readonly property var titles: ({ invoice: qsTr("Invoice"), estimate: qsTr("Estimate"), salesReceipt: qsTr("Sales receipt"),
                                     creditMemo: qsTr("Credit memo"), bill: qsTr("Bill"), expense: qsTr("Expense"),
                                     check: qsTr("Check"), vendorCredit: qsTr("Vendor credit") })
    property var parties: []
    property var paymentAccounts: []
    property var terms: []
    property var taxRates: []

    objectName: "documentEditor"
    title: (existing ? "" : qsTr("New ")) + (titles[kind] || kind) + (existing && doc.number ? " " + doc.number : "")
    subtitle: existing ? String(doc.statusLabel || "") : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: voided ? qsTr("Close") : qsTr("Save and close")
    secondaryText: voided ? "" : qsTr("Cancel")
    tertiaryText: !existing && (kind === "invoice" || kind === "estimate") ? qsTr("Save as draft") : ""
    primaryEnabled: voided || (partyBox.currentIndex > 0 && doc.lines && doc.lines.length > 0)

    function openFor(k, id) {
        const loaded = Number(id || 0) > 0 ? books.document(id) : null
        kind = loaded !== null ? String(loaded.kind || k || "invoice") : (k || "invoice")
        parties = [{ id: 0, name: sales ? qsTr("Choose a customer") : qsTr("Choose a payee") }]
                  .concat(books.parties(sales ? "customer" : "vendor", false))
        paymentAccounts = books.paymentAccounts()
        terms = books.terms()
        taxRates = [{ id: 0, name: qsTr("No sales tax") }].concat(books.taxRates())
        if (loaded !== null) {
            doc = loaded
        } else {
            const settings = books.settings
            doc = { kind: kind, date: books.today, termsId: settings.defaultTermsId || 0, notes: sales ? (settings.invoiceNotes || "") : "",
                    lines: [{ itemId: 0, accountId: 0, description: "", quantity: "1", rate: "", taxable: sales }] }
        }
        error.visible = false
        recompute()
        open()
    }
    function edit(field, value) { const next = Object.assign({}, doc); next[field] = value; doc = next; recompute() }
    function recompute() { totals = books !== null ? books.computeTotals(Object.assign({}, doc, { kind: kind })) : ({}) }
    function save(state) {
        const result = books.saveDocument(Object.assign({}, doc, { kind: kind, state: state }))
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; open(); return false }
        host.setPrintSource("document", result.id, ({}))
        return true
    }
    onAccepted: if (!voided) save("open")
    onTertiary: save("draft")

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Notice { visible: editor.voided; variant: "warning"; title: qsTr("Voided"); text: qsTr("This document was voided; it stays in the records with no effect on the books.") }

        Tk.Grid {
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: editor.sales ? qsTr("Customer") : qsTr("Payee")
                Tk.ComboBox {
                    id: partyBox
                    objectName: "documentParty"
                    small: true
                    enabled: !editor.voided
                    textRole: "name"
                    model: editor.parties
                    currentIndex: Math.max(0, editor.parties.map(function(p) { return p.id }).indexOf(editor.doc.partyId || 0))
                    onActivated: function(i) { editor.edit("partyId", editor.parties[i].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Date")
                Tk.TextField { objectName: "documentDate"; small: true; mono: true; readOnly: editor.voided; text: String(editor.doc.date || ""); onEditingFinished: editor.edit("date", text) }
            }
            Tk.PropertyRow {
                label: editor.kind === "estimate" ? qsTr("Expires") : editor.paidNow ? qsTr("Paid from") : qsTr("Terms")
                Tk.ComboBox {
                    objectName: editor.paidNow ? "documentAccount" : "documentTerms"
                    small: true
                    enabled: !editor.voided
                    textRole: "name"
                    model: editor.paidNow ? editor.paymentAccounts : editor.terms
                    currentIndex: Math.max(0, model.map(function(m) { return m.id }).indexOf(
                        editor.paidNow ? (editor.doc.depositAccountId || editor.doc.paymentAccountId || 0) : (editor.doc.termsId || 0)))
                    onActivated: function(i) {
                        if (!editor.paidNow) editor.edit("termsId", model[i].id)
                        else editor.edit(editor.sales ? "depositAccountId" : "paymentAccountId", model[i].id)
                    }
                }
            }
            Tk.PropertyRow {
                label: editor.kind === "check" ? qsTr("Check no.") : qsTr("Number")
                Tk.TextField { objectName: "documentNumber"; small: true; mono: true; readOnly: editor.voided; placeholderText: qsTr("Automatic"); text: String(editor.doc.number || ""); onEditingFinished: editor.edit("number", text) }
            }
            Tk.PropertyRow {
                visible: !editor.paidNow && editor.kind !== "creditMemo" && editor.kind !== "vendorCredit"
                label: qsTr("Due")
                Tk.Label { objectName: "documentDue"; text: String(editor.totals.dueDate || editor.doc.dueDate || "—") }
            }
            Tk.PropertyRow {
                visible: editor.sales
                label: qsTr("Sales tax")
                Tk.ComboBox {
                    objectName: "documentTax"
                    small: true
                    enabled: !editor.voided
                    textRole: "name"
                    model: editor.taxRates
                    currentIndex: Math.max(0, editor.taxRates.map(function(t) { return t.id }).indexOf(editor.doc.taxRateId || 0))
                    onActivated: function(i) { editor.edit("taxRateId", editor.taxRates[i].id) }
                }
            }
        }

        LineItemsTable {
            objectName: "documentLines"
            lines: editor.doc.lines || []
            mode: editor.sales ? "item" : "account"
            items: editor.books !== null ? editor.books.items(false) : []
            accounts: editor.books !== null ? editor.books.expenseAccounts() : []
            showTax: editor.sales
            readOnly: editor.voided
            amounts: (editor.totals.lines || []).map(function(l) { return l.amountText })
            onEdited: function(lines) { editor.edit("lines", lines) }
        }

        Tk.Grid {
            columns: "3fr 2fr"
            columnGap: Tk.Theme.space.xl
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.TextArea { objectName: "documentNotes"; visible: editor.sales; rows: 3; readOnly: editor.voided; placeholderText: qsTr("Note to the customer (printed)"); text: String(editor.doc.notes || ""); onEditingFinished: editor.edit("notes", text) }
                Tk.TextArea { objectName: "documentMemo"; rows: 2; readOnly: editor.voided; placeholderText: qsTr("Internal memo (not printed)"); text: String(editor.doc.memo || ""); onEditingFinished: editor.edit("memo", text) }
            }
            Tk.Box {
                objectName: "documentTotals"
                color: Tk.Theme.color.panelAlt
                radius: Tk.Theme.radius.md
                padding: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Tk.KeyValue { key: qsTr("Subtotal"); value: String(editor.totals.subtotalText || "") }
                    Tk.Flex {
                        visible: editor.sales
                        direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                        Tk.Caption { text: qsTr("Discount %"); Tk.Flex.grow: 1 }
                        Tk.TextField { objectName: "documentDiscount"; small: true; mono: true; implicitWidth: Tk.Theme.size.controlLg * 2.2; horizontalAlignment: Text.AlignRight
                                       readOnly: editor.voided; placeholderText: "0"; text: String(editor.doc.discountPercent || ""); onEditingFinished: editor.edit("discountPercent", text) }
                        Tk.Mono { text: String(editor.totals.discountText || "") }
                    }
                    Tk.KeyValue { visible: editor.sales; key: qsTr("Sales tax"); value: String(editor.totals.taxText || "") }
                    Tk.Divider { }
                    Tk.KeyValue { key: qsTr("Total"); value: String(editor.totals.totalText || ""); accent: true }
                    Tk.KeyValue { visible: editor.existing && !editor.paidNow; key: qsTr("Balance due"); value: String(editor.doc.balanceText || "") }
                }
            }
        }

        Tk.Flex {
            objectName: "documentActions"
            visible: editor.existing
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { small: true; variant: "ghost"; iconName: "printer"; text: qsTr("Print"); onClicked: { editor.host.setPrintSource("document", editor.doc.id, ({})); editor.host.actions.trigger("filePrintAction") } }
            Tk.Button { small: true; variant: "ghost"; iconName: "file-down"; text: qsTr("Save PDF…"); onClicked: pdfDialog.open() }
            Tk.Button { small: true; variant: "ghost"; iconName: "send"; text: qsTr("Email"); visible: editor.sales; onClicked: editor.books.emailDocument(editor.doc.id) }
            Tk.Button { small: true; variant: "ghost"; iconName: "file-check"; text: qsTr("Convert to invoice"); visible: editor.kind === "estimate" && !editor.voided
                        onClicked: { const r = editor.books.convertEstimate(editor.doc.id); if (r.ok) editor.openFor("invoice", r.id) } }
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
        title: qsTr("Void this document")
        label: qsTr("Why? The reason is kept in the audit trail.")
        primaryText: qsTr("Void")
        destructive: true
        onAccepted: { editor.books.voidDocument(editor.doc.id, text); editor.close() }
    }
}
