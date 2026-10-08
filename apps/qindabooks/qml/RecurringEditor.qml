// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Create or edit one recurring-invoice schedule (M63, D-071): its name and
// customer, the invoice body (LineItemsTable, sales tax) and the rhythm —
// every N days/weeks/months/years, on a day of the month, from the next
// invoice date, until a date or for N more invoices — and whether the
// invoices are posted or kept as drafts. Totals come from
// BooksService.computeTotals; this dialog never adds money.
//
// AGENT-NOTE: for an existing schedule the date field is the NEXT invoice
// date and the count is the invoices still to come; BooksService passes an
// unchanged schedule back as it was, and the engine re-bases a changed
// one from that date (Recurring::update).
Tk.Dialog {
    id: editor

    property BooksService books: null
    property var schedule: ({})      // recurringTemplate(id), or {} for a new one
    property var doc: ({})           // the invoice body as a document draft
    property var totals: ({})
    property var customers: []
    property var taxRates: []
    property string problem: ""
    readonly property bool existing: Number(schedule.id || 0) > 0
    readonly property var frequencies: [{ text: qsTr("Daily"), value: "daily" }, { text: qsTr("Weekly"), value: "weekly" },
                                        { text: qsTr("Monthly"), value: "monthly" }, { text: qsTr("Yearly"), value: "yearly" }]
    readonly property string frequency: frequencies[Math.max(0, frequencyBox.currentIndex)].value

    objectName: "recurringEditor"
    title: existing ? String(schedule.name || "") : qsTr("New recurring invoice")
    subtitle: existing ? qsTr("%n invoice(s) made so far", "", schedule.generated || 0) : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    primaryEnabled: nameField.text.trim().length > 0 && customerBox.currentIndex > 0 && (doc.lines || []).length > 0

    function openFor(id) {
        customers = [{ id: 0, name: qsTr("Choose a customer") }].concat(books.parties("customer", false))
        taxRates = [{ id: 0, name: qsTr("No sales tax") }].concat(books.taxRates())
        schedule = Number(id || 0) > 0 ? books.recurringTemplate(id) : ({ frequency: "monthly", interval: 1, postOpen: true,
                                                                          nextDate: books.today, active: true })
        doc = existing ? Object.assign({}, schedule.document)
                       : { lines: [{ itemId: 0, accountId: 0, description: "", quantity: "1", rate: "", taxable: true }] }
        nameField.text = String(schedule.name || "")
        customerBox.currentIndex = Math.max(0, customers.map(function(c) { return c.id }).indexOf(schedule.customerId || 0))
        frequencyBox.currentIndex = Math.max(0, frequencies.map(function(f) { return f.value }).indexOf(String(schedule.frequency || "monthly")))
        intervalField.value = Number(schedule.interval || 1)
        dayField.value = Number(schedule.dayOfMonth || 0)
        nextField.text = String(schedule.nextDate || books.today)
        endField.text = String(schedule.end || "")
        countField.value = Number(existing ? (schedule.remaining || 0) : (schedule.maxOccurrences || 0))
        postBox.currentIndex = schedule.postOpen === false ? 1 : 0
        problem = ""
        recompute()
        open()
        nameField.forceActiveFocus()
    }
    function edit(field, value) { const next = Object.assign({}, doc); next[field] = value; doc = next; recompute() }
    function recompute() { totals = books !== null ? books.computeTotals(Object.assign({}, doc, { kind: "invoice" })) : ({}) }
    function collect() {
        return { id: Number(schedule.id || 0), name: nameField.text.trim(), frequency: editor.frequency,
                 interval: Math.round(intervalField.value), dayOfMonth: Math.round(dayField.value),
                 nextDate: nextField.text.trim(), end: endField.text.trim(), maxOccurrences: Math.round(countField.value),
                 postOpen: postBox.currentIndex === 0,
                 document: Object.assign({}, doc, { partyId: customers[customerBox.currentIndex].id }) }
    }
    // The save step; the tests fill the fields and call it directly.
    function save() {
        const r = books.saveRecurring(collect())
        problem = r.ok ? "" : String(r.error || "")
        return r
    }
    onAccepted: { if (!save().ok) open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "recurringEditorProblem"; visible: editor.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: editor.problem }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "recurringName"; small: true; placeholderText: qsTr("Monthly retainer") } }
            Tk.PropertyRow {
                label: qsTr("Customer")
                Tk.ComboBox { id: customerBox; objectName: "recurringCustomer"; small: true; textRole: "name"; model: editor.customers }
            }
            Tk.PropertyRow { label: qsTr("Repeats"); Tk.Segmented { id: frequencyBox; objectName: "recurringFrequency"; small: true; model: editor.frequencies } }
            Tk.PropertyRow {
                label: qsTr("Every")
                Tk.NumberField { id: intervalField; objectName: "recurringInterval"; small: true; from: 1; to: 365; decimals: 0
                                 suffix: editor.frequency === "daily" ? qsTr(" day(s)") : editor.frequency === "weekly" ? qsTr(" week(s)")
                                       : editor.frequency === "monthly" ? qsTr(" month(s)") : qsTr(" year(s)") }
            }
            Tk.PropertyRow {
                label: editor.existing ? qsTr("Next invoice") : qsTr("First invoice")
                Tk.TextField { id: nextField; objectName: "recurringNextDate"; small: true; mono: true; placeholderText: "yyyy-mm-dd" }
            }
            Tk.PropertyRow {
                visible: editor.frequency === "monthly" || editor.frequency === "yearly"
                label: qsTr("Day of month")
                Tk.NumberField { id: dayField; objectName: "recurringDay"; small: true; from: 0; to: 31; decimals: 0
                                 suffix: value === 0 ? qsTr(" (the invoice date's day)") : ""
                                 tooltip: qsTr("0 = the day of the invoice date; 31 = the last day of each month") }
            }
            Tk.PropertyRow {
                label: qsTr("Ends on")
                Tk.TextField { id: endField; objectName: "recurringEnd"; small: true; mono: true; placeholderText: qsTr("never") }
            }
            Tk.PropertyRow {
                label: editor.existing ? qsTr("Invoices to come") : qsTr("Invoices in all")
                Tk.NumberField { id: countField; objectName: "recurringCount"; small: true; from: 0; to: 9999; decimals: 0
                                 suffix: value === 0 ? qsTr(" (no limit)") : ""
                                 tooltip: qsTr("0 = no limit") }
            }
            Tk.PropertyRow {
                label: qsTr("Invoices are")
                Tk.Segmented { id: postBox; objectName: "recurringPost"; small: true; model: [qsTr("Posted"), qsTr("Drafts")] }
            }
            Tk.PropertyRow {
                label: qsTr("Sales tax")
                Tk.ComboBox {
                    objectName: "recurringTax"
                    small: true
                    textRole: "name"
                    model: editor.taxRates
                    currentIndex: Math.max(0, editor.taxRates.map(function(t) { return t.id }).indexOf(editor.doc.taxRateId || 0))
                    onActivated: function(i) { editor.edit("taxRateId", editor.taxRates[i].id) }
                }
            }
        }
        LineItemsTable {
            objectName: "recurringLines"
            lines: editor.doc.lines || []
            mode: "item"
            items: editor.books !== null ? editor.books.items(false) : []
            accounts: editor.books !== null ? editor.books.incomeAccounts() : []
            showTax: true
            amounts: (editor.totals.lines || []).map(function(l) { return l.amountText })
            onEdited: function(lines) { editor.edit("lines", lines) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xl
            Tk.Spacer { }
            // A KeyValue in a row needs a width (its key has flex basis 0).
            Tk.KeyValue { implicitWidth: Tk.Theme.size.tableColumn * 2; key: qsTr("Subtotal"); value: String(editor.totals.subtotalText || "") }
            Tk.KeyValue { implicitWidth: Tk.Theme.size.tableColumn * 2; key: qsTr("Sales tax"); value: String(editor.totals.taxText || "") }
            Tk.KeyValue { objectName: "recurringTotal"; implicitWidth: Tk.Theme.size.tableColumn * 2.2; key: qsTr("Each invoice"); value: String(editor.totals.totalText || ""); accent: true }
        }
    }
}
