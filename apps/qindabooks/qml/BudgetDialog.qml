// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Accounting ▸ Budgets… (M63, D-071): what the business plans to earn and
// spend, per income and expense account, for one fiscal year — the numbers
// Reports ▸ Budget vs. Actual compares against. A row takes a year's
// amount (split evenly and exactly into its months by the service) or, when
// opened, each month. Money stays text here: BooksService.budgetSpread and
// budgetTotal do the arithmetic.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property int year: 0
    property var rows: []          // budget(year).rows, edited as copies
    property var labels: []        // the fiscal months
    property string period: ""
    property int expanded: -1      // the row showing its months
    property string problem: ""
    property bool dirty: false

    objectName: "budgetDialog"
    title: qsTr("Budgets")
    subtitle: period
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: qsTr("Save")
    primaryEnabled: dirty
    secondaryText: qsTr("Cancel")
    tertiaryText: qsTr("Budget vs. Actual")

    function openFor(fiscalYear) { load(fiscalYear); open() }
    function load(fiscalYear) {
        const b = books.budget(fiscalYear)
        year = b.fiscalYear; rows = b.rows || []; labels = b.monthLabels || []
        period = qsTr("Fiscal year %1 · %2 to %3").arg(b.fiscalYear).arg(b.from).arg(b.to)
        expanded = -1; problem = ""; dirty = false
    }
    function setRow(i, row) { const next = rows.slice(); next[i] = row; rows = next; dirty = true }
    // A year's amount, split into months by the service.
    function setAnnual(i, text) {
        const r = books.budgetSpread(text)
        if (!r.ok) { problem = String(r.error || ""); return }
        problem = ""
        setRow(i, Object.assign({}, rows[i], { months: r.months, annualText: r.annualText }))
    }
    function setMonth(i, m, text) {
        const months = rows[i].months.slice()
        months[m] = text
        const t = books.budgetTotal(months)
        if (!t.ok) { problem = String(t.error || ""); return }
        problem = ""
        setRow(i, Object.assign({}, rows[i], { months: months, annualText: t.totalText }))
    }
    // The save step; the tests call it directly.
    function save() {
        const r = books.saveBudget({ fiscalYear: year, rows: rows.map(function(row) { return { accountId: row.accountId, months: row.months } }) })
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) dirty = false
        return r
    }
    onAccepted: { if (!save().ok) open() }
    onTertiary: {
        if (dirty && !save().ok) return
        close()
        host.reportRequested("budget_vs_actual")
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "budgetProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not saved"); text: dialog.problem }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.IconButton { objectName: "budgetPrevYear"; small: true; iconName: "chevron-left"; tooltip: qsTr("Previous fiscal year"); enabled: !dialog.dirty; onClicked: dialog.load(dialog.year - 1) }
            Tk.Label { objectName: "budgetYear"; text: qsTr("Fiscal year %1").arg(dialog.year) }
            Tk.IconButton { objectName: "budgetNextYear"; small: true; iconName: "chevron-right"; tooltip: qsTr("Next fiscal year"); enabled: !dialog.dirty; onClicked: dialog.load(dialog.year + 1) }
            Tk.Spacer { }
            Tk.Caption { text: qsTr("Amounts are what you expect to earn or spend; months split evenly unless you set them.") }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Account"); Tk.Flex.grow: 1 }
            Tk.Overline { title: qsTr("For the year"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.4; Tk.Flex.shrink: 0 }
            Item { implicitWidth: Tk.Theme.size.control }
        }
        Tk.Divider { }
        Repeater {
            model: dialog.rows.length
            delegate: Tk.Flex {
                id: line
                required property int index
                readonly property var row: dialog.rows[index] || ({})
                readonly property bool startsSection: index === 0 || dialog.rows[index - 1].section !== row.section
                objectName: "budgetRow_" + index
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.SectionHeader { visible: line.startsSection; title: line.row.section === "income" ? qsTr("Income") : qsTr("Costs and expenses") }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Label { text: "    ".repeat(line.row.depth || 0) + String(line.row.name || ""); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.TextField {
                        objectName: "budgetAnnual"
                        implicitWidth: Tk.Theme.size.tableColumn * 1.4
                        small: true
                        mono: true
                        horizontalAlignment: Text.AlignRight
                        placeholderText: "0.00"
                        text: String(line.row.annualText || "")
                        onEditingFinished: if (text !== String(line.row.annualText || "")) dialog.setAnnual(line.index, text)
                    }
                    Tk.IconButton {
                        objectName: "budgetMonths"
                        small: true
                        iconName: dialog.expanded === line.index ? "chevron-up" : "chevron-down"
                        tooltip: qsTr("Month by month")
                        onClicked: dialog.expanded = dialog.expanded === line.index ? -1 : line.index
                    }
                }
                Tk.Grid {
                    visible: dialog.expanded === line.index
                    columns: "1fr 1fr 1fr 1fr 1fr 1fr"
                    columnGap: Tk.Theme.space.sm
                    rowGap: Tk.Theme.space.xs
                    Repeater {
                        model: dialog.expanded === line.index ? 12 : 0
                        delegate: Tk.Flex {
                            id: month
                            required property int index
                            direction: Tk.Flex.Column
                            gap: 0
                            Tk.Caption { text: String(dialog.labels[month.index] || "") }
                            Tk.TextField {
                                objectName: "budgetMonth_" + month.index
                                small: true
                                mono: true
                                horizontalAlignment: Text.AlignRight
                                placeholderText: "0.00"
                                text: String((line.row.months || [])[month.index] || "")
                                onEditingFinished: if (text !== String((line.row.months || [])[month.index] || "")) dialog.setMonth(line.index, month.index, text)
                            }
                        }
                    }
                }
            }
        }
        Tk.Caption { visible: dialog.rows.length === 0; text: qsTr("The chart of accounts has no income or expense accounts.") }
    }
}
