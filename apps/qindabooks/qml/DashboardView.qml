// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// The dashboard (D-047): this month and this year at a glance, what money
// is owed in and out, cash, a 12-month trend, where the money went, the
// health ratios with their definitions, and the day's shortcuts. It reads
// BooksService.kpis/monthlyTrends/expenseBreakdown/ratios and refreshes on
// every committed change.
Tk.Scroll {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property var kpis: ({})
    property var trends: []
    property var breakdown: []
    property var ratios: []

    function refresh() {
        if (books === null || !books.hasCompany) return
        kpis = books.kpis(books.today)
        trends = books.monthlyTrends(12)
        const yearStart = books.rangePreset("yearToDate")
        breakdown = books.expenseBreakdown(yearStart.from, yearStart.to)
        ratios = books.ratios(books.today)
        trendGraph.clear()
        for (const month of trends) trendGraph.appendRow([month.incomeChart, month.expensesChart])
    }
    Connections { target: view.books; function onChanged() { view.refresh() } }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()

    Tk.Flex {
        width: view.viewportWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.lg
        padding: Tk.Theme.space.xl

        Tk.Flex {
            objectName: "dashHeader"
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Flex.grow: 1
                Tk.Heading { objectName: "dashCompanyHeading"; level: 1; text: view.books !== null ? view.books.companyName : "" }
                Tk.Caption { text: qsTr("As of %1").arg(view.books !== null ? view.books.today : "") }
            }
            Tk.Button { objectName: "dashNewInvoice"; variant: "accent"; iconName: "receipt"; text: qsTr("New invoice"); onClicked: view.host.actions.trigger("booksNewInvoiceAction") }
            Tk.Button { objectName: "dashReceivePayment"; variant: "outline"; iconName: "hand-coins"; text: qsTr("Receive payment"); onClicked: view.host.actions.trigger("booksReceivePaymentAction") }
            Tk.Button { objectName: "dashNewExpense"; variant: "outline"; iconName: "wallet"; text: qsTr("New expense"); onClicked: view.host.actions.trigger("booksNewExpenseAction") }
        }

        Tk.Notice {
            visible: view.books !== null && view.books.dueRecurringCount > 0
            variant: "info"
            title: qsTr("Recurring transactions are due")
            text: qsTr("%n recurring transaction(s) can be created now.", "", view.books !== null ? view.books.dueRecurringCount : 0)
            Tk.Button { small: true; text: qsTr("Create them"); onClicked: view.books.generateDueRecurring() }
        }

        Tk.Grid {
            objectName: "dashKpis"
            columns: "repeat(auto-fill, minmax(" + Tk.Theme.size.panelMinWidth + ", 1fr))"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md
            KpiCard { objectName: "kpiIncome"; label: qsTr("Income this month"); value: view.kpis.incomeMtdText || "—"; caption: qsTr("%1 this year").arg(view.kpis.incomeYtdText || "—"); iconName: "trending-up"; tone: "good" }
            KpiCard { objectName: "kpiExpenses"; label: qsTr("Expenses this month"); value: view.kpis.expensesMtdText || "—"; caption: qsTr("%1 this year").arg(view.kpis.expensesYtdText || "—"); iconName: "trending-down" }
            KpiCard { objectName: "kpiNet"; label: qsTr("Net income this month"); value: view.kpis.netMtdText || "—"; caption: qsTr("%1 this year").arg(view.kpis.netYtdText || "—"); iconName: "scale"; tone: (view.kpis.netMtd || 0) < 0 ? "danger" : "good" }
            KpiCard { objectName: "kpiCash"; label: qsTr("Cash"); value: view.kpis.cashText || "—"; caption: qsTr("Across bank accounts"); iconName: "landmark"; onClicked: view.host.actions.trigger("booksViewBankingAction") }
            KpiCard { objectName: "kpiOpen"; label: qsTr("Open invoices"); value: view.kpis.openInvoicesText || "—"; caption: qsTr("Owed to you"); iconName: "receipt"; onClicked: view.host.actions.trigger("booksViewSalesAction") }
            KpiCard { objectName: "kpiOverdue"; label: qsTr("Overdue"); value: view.kpis.overdueAmountText || "—"; caption: qsTr("%n invoice(s) past due", "", view.kpis.overdueInvoices || 0); iconName: "file-clock"; tone: (view.kpis.overdueInvoices || 0) > 0 ? "danger" : "good"; onClicked: view.host.actions.trigger("booksViewSalesAction") }
            KpiCard { objectName: "kpiBills"; label: qsTr("Unpaid bills"); value: view.kpis.unpaidBillsText || "—"; caption: qsTr("You owe"); iconName: "file-check"; tone: "warning"; onClicked: view.host.actions.trigger("booksViewExpensesAction") }
        }

        Tk.Grid {
            columns: "2fr 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md
            Tk.Card {
                objectName: "dashTrend"
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Flex {
                        direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.md
                        Tk.Overline { title: qsTr("Last 12 months"); Tk.Flex.grow: 1 }
                        Tk.Badge { text: qsTr("Income"); variant: "success" }
                        Tk.Badge { text: qsTr("Expenses"); variant: "warning" }
                    }
                    Tk.Graph {
                        id: trendGraph
                        objectName: "dashTrendGraph"
                        implicitHeight: Tk.Theme.size.thumbnail * 2.5
                        capacity: 12
                        autoScale: true
                        gridRows: 4
                        gridColumns: 12
                        Tk.GraphSeries { label: qsTr("Income"); color: Tk.Theme.color.success; fill: true; fillOpacity: Tk.Theme.opacity.tint }
                        Tk.GraphSeries { label: qsTr("Expenses"); color: Tk.Theme.color.warning; fill: false; lineWidth: 2 }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        Repeater {
                            model: view.trends
                            delegate: Tk.Caption {
                                required property var modelData
                                Tk.Flex.grow: 1; Tk.Flex.basis: 0
                                horizontalAlignment: Text.AlignHCenter
                                text: String(modelData.label || "")
                            }
                        }
                    }
                }
            }
            Tk.Card {
                objectName: "dashBreakdown"
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Overline { title: qsTr("Where the money went this year") }
                    Tk.Caption { visible: view.breakdown.length === 0; text: qsTr("No expenses recorded yet.") }
                    Repeater {
                        model: view.breakdown.slice(0, 7)
                        delegate: Tk.Flex {
                            required property var modelData
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                Tk.Label { text: String(modelData.label); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.Mono { text: String(modelData.amountText) }
                            }
                            Tk.Meter { from: 0; to: 100; value: Number(modelData.share || 0); implicitWidth: parent.width }
                        }
                    }
                }
            }
        }

        Tk.Card {
            objectName: "dashRatios"
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("Financial health") }
                Tk.Grid {
                    columns: "repeat(auto-fill, minmax(" + Tk.Theme.size.panelMinWidth * 1.2 + ", 1fr))"
                    columnGap: Tk.Theme.space.xl
                    Repeater {
                        model: view.ratios
                        delegate: Tk.KeyValue {
                            required property var modelData
                            key: String(modelData.label)
                            value: String(modelData.valueText)
                            accent: modelData.good === true
                            Tk.ToolTip { text: String(parent.modelData.definition || ""); visible: hover.hovered }
                            HoverHandler { id: hover }
                        }
                    }
                }
            }
        }
    }
}
