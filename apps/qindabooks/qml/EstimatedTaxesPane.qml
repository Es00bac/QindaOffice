// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Estimated taxes (M71, D-081): the planner for a sole
// proprietor or partner — this year's profit from the books, the federal
// (Form 1040-ES) tax on it with self-employment tax, and Colorado's
// (DR 0104EP) for a business that chose Colorado, the required annual
// payment (the safe harbor), the four installments with what was paid and
// what is still owed, and the worksheets line by line. Payments made
// through EFTPS, IRS Direct Pay or the state's system are recorded here. A
// corporation or nonprofit gets the reason instead (PayrollService::estimate).
Tk.Scroll {
    id: pane

    property BooksService books: null
    property PayrollService payroll: null
    property var plan: ({})
    readonly property bool ready: plan.ok === true

    signal profileRequested()
    signal paymentRequested(string jurisdiction, int installment, string amountPlain, int year)

    function refresh() { plan = payroll !== null && books !== null && books.hasCompany ? payroll.estimate() : ({}) }
    onVisibleChanged: if (visible) refresh()

    // AGENT-NOTE: an inline component cannot see this file's ids; it
    // reports the picked installment and the user of it acts.
    component Installments: Tk.DataTable {
        id: table
        property var rows: []
        signal picked(var row)
        model: rows
        implicitHeight: Tk.Theme.size.row * 5.5
        onActivated: function(index) { table.picked(table.rows[index]) }
        columns: [
            Tk.TableColumn { key: "number"; title: qsTr("Payment"); width: Tk.Theme.size.tableColumn * 0.9; mono: true },
            Tk.TableColumn { key: "due"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn {
                key: "state"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn * 1.4
                delegate: Item {
                    Tk.Badge {
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.parent && parent.parent.row ? String(parent.parent.row.stateLabel || "") : ""
                        variant: !(parent.parent && parent.parent.row) ? "default"
                               : parent.parent.row.state === "paid" ? "success"
                               : parent.parent.row.state === "overdue" ? "danger"
                               : parent.parent.row.state === "paidLate" ? "warning" : "default"
                    }
                }
            },
            Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "paidText"; title: qsTr("Paid"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "owedText"; title: qsTr("Still owed"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true }
        ]
    }

    Tk.Flex {
        width: pane.viewportWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Notice {
            objectName: "estimateProblem"
            visible: pane.plan.ok === false
            variant: "info"
            title: qsTr("No estimate")
            text: String(pane.plan.error || "")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Heading { level: 3; text: pane.ready ? qsTr("%1 estimated taxes").arg(pane.plan.year) : qsTr("Estimated taxes"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "estimateProfile"; variant: "outline"; iconName: "user-round"; text: qsTr("Owner's tax picture…"); onClicked: pane.profileRequested() }
            Tk.Button {
                objectName: "estimateRecord"; variant: "accent"; iconName: "landmark"; text: qsTr("Record a payment…")
                enabled: pane.ready
                onClicked: pane.paymentRequested("federal", pane.nextInstallment(pane.plan.federal), pane.owedOn(pane.plan.federal), pane.plan.year)
            }
        }
        Tk.Flex {
            visible: pane.ready
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.md
            KpiCard { Tk.Flex.grow: 1; objectName: "estimateProfit"; label: qsTr("Profit so far"); value: String(pane.plan.ytdProfitText || ""); caption: qsTr("from the books, to %1").arg(pane.plan.asOf || ""); iconName: "chart-line" }
            KpiCard {
                Tk.Flex.grow: 1
                objectName: "estimateYear"; label: qsTr("For the year"); value: String(pane.plan.annualProfitText || ""); iconName: "calendar-clock"
                caption: pane.plan.annualized ? qsTr("projected from the profit so far") : qsTr("the figure entered")
            }
            KpiCard { Tk.Flex.grow: 1; objectName: "estimateFederalTax"; label: qsTr("Federal tax"); value: pane.ready ? String(pane.plan.federal.totalTaxText) : ""; caption: qsTr("self-employment %1").arg(pane.plan.seTaxText || ""); iconName: "landmark" }
            KpiCard { visible: pane.plan.colorado !== undefined && pane.plan.colorado !== null; Tk.Flex.grow: 1; objectName: "estimateColoradoTax"; label: qsTr("Colorado tax"); value: String(pane.plan.coloradoTaxText || ""); caption: qsTr("income tax"); iconName: "map-pin" }
            KpiCard {
                Tk.Flex.grow: 1
                objectName: "estimateSetAside"; label: qsTr("Set aside"); value: pane.ready ? qsTr("%1%").arg(pane.plan.setAsidePercent) : ""; iconName: "piggy-bank"; tone: "good"
                caption: qsTr("of each draw, for these taxes")
            }
        }
        Repeater {
            // Colorado's plan only when the service gives one (a business
            // that chose Colorado).
            model: !pane.ready ? []
                 : [{ key: "federal", title: qsTr("Federal — Form 1040-ES") }].concat(
                       pane.plan.colorado ? [{ key: "colorado", title: qsTr("Colorado — DR 0104EP") }] : [])
            delegate: Tk.Flex {
                id: section
                required property var modelData
                readonly property var part: pane.plan[modelData.key] || ({})
                objectName: "estimate_" + modelData.key
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.SectionHeader { title: section.modelData.title; count: String(section.part.totalTaxText || "") }
                Tk.Caption {
                    wrapMode: Text.Wrap
                    text: section.part.required
                          ? qsTr("Required this year: %1 (%2). Paid so far: %3; still to pay: %4.").arg(section.part.requiredText).arg(section.part.basis)
                                .arg(section.part.paidText).arg(section.part.remainingText)
                          : String(section.part.reason || "")
                }
                Installments {
                    objectName: "estimateInstallments_" + section.modelData.key
                    rows: section.part.installments || []
                    onPicked: function(row) { pane.paymentRequested(section.modelData.key, row.number, String(row.owedPlain || ""), pane.plan.year) }
                }
                Tk.SectionHeader { id: sheetHeader; title: qsTr("Worksheet"); collapsible: true; collapsed: true }
                WorksheetLines { visible: !sheetHeader.collapsed; lines: section.part.worksheet || [] }
            }
        }
        Tk.SectionHeader { id: seHeader; visible: pane.ready; title: qsTr("Self-employment tax (Schedule SE)"); count: String(pane.plan.seTaxText || ""); collapsible: true; collapsed: true }
        WorksheetLines { objectName: "estimateSeWorksheet"; visible: pane.ready && !seHeader.collapsed; lines: pane.plan.seWorksheet || [] }
        Repeater {
            model: pane.ready ? (pane.plan.notes || []) : []
            delegate: Tk.Caption { required property var modelData; wrapMode: Text.Wrap; text: String(modelData) }
        }
    }

    function nextInstallment(part) {
        for (const i of (part && part.installments) || []) if (Number(i.owed) > 0) return i.number
        return 1
    }
    function owedOn(part) {
        for (const i of (part && part.installments) || []) if (Number(i.owed) > 0) return String(i.owedPlain)
        return ""
    }
}
