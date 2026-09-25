// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Banking (D-047): the bank and card accounts on the left; for the chosen
// one, the imported lines waiting for review (accept the suggested match,
// categorize, or exclude), its register, and the bank rules. Statements
// come in as CSV, OFX/QFX or QIF; duplicates are skipped by the engine.
// Reconciling replaces the tabs with ReconcileView until it is finished.
Item {
    id: view

    property BooksService books: null
    property BooksHost host: null
    property var accounts: []
    property int accountId: 0
    property int tab: 0
    property var rows: []
    property int sessionId: 0
    readonly property var account: accounts.find(function(a) { return a.id === view.accountId }) || ({})

    function refresh() {
        if (books === null || !books.hasCompany) return
        accounts = books.bankAccounts()
        if (accountId === 0 && accounts.length > 0) accountId = accounts[0].id
        if (accountId === 0) { rows = []; return }
        if (tab === 0) rows = books.feed(accountId, "new")
        else if (tab === 1) { const r = books.rangePreset("last12Months"); rows = books.accountRegister(accountId, r.from, r.to) }
        else rows = books.rules()
    }
    function importStatement() { importDialog.open() }
    function startReconcile() { reconcileStart.openFor(view.account) }
    onAccountIdChanged: refresh()
    onTabChanged: refresh()
    onVisibleChanged: if (visible) refresh()
    Connections { target: view.books; function onChanged() { view.refresh() } }

    Tk.Splitter {
        anchors.fill: parent
        orientation: Qt.Horizontal
        Tk.Box {
            objectName: "bankAccountsPane"
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.3
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
            color: Tk.Theme.color.panel
            padding: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.SectionHeader { title: qsTr("Accounts") }
                Repeater {
                    model: view.accounts
                    delegate: Tk.ListRow {
                        required property var modelData
                        objectName: "bankAccount_" + modelData.id
                        iconName: modelData.type === "CreditCard" ? "credit-card" : "landmark"
                        text: String(modelData.name)
                        secondaryText: String(modelData.balanceText || "")
                        selected: view.accountId === modelData.id
                        onClicked: view.accountId = modelData.id
                        trailing: Tk.Badge { visible: (modelData.unreviewed || 0) > 0; text: String(modelData.unreviewed || 0); variant: "accent"; tooltip: qsTr("Lines to review") }
                    }
                }
                Tk.Caption { visible: view.accounts.length === 0; text: qsTr("Add a Bank or Credit Card account in Accounting."); wrapMode: Text.Wrap; width: parent.width }
                Tk.Spacer { }
                Tk.Button { objectName: "bankImport"; variant: "accent"; iconName: "file-input"; text: qsTr("Import statement…"); enabled: view.accountId > 0; onClicked: view.importStatement() }
                Tk.Button { objectName: "bankReconcile"; variant: "outline"; iconName: "scale"; text: qsTr("Reconcile…"); enabled: view.accountId > 0; onClicked: view.startReconcile() }
            }
        }
        Tk.Box {
            T.SplitView.fillWidth: true
            color: Tk.Theme.color.surface
            padding: Tk.Theme.space.lg
            Tk.Flex {
                anchors.fill: parent
                visible: view.sessionId === 0
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.md
                    Tk.Heading { level: 2; text: String(view.account.name || qsTr("Banking")); Tk.Flex.grow: 1; elide: Text.ElideRight }
                    Tk.KeyValue { key: qsTr("Balance in the books"); value: String(view.account.balanceText || "") }
                }
                Tk.TabStrip {
                    objectName: "bankTabs"
                    model: [qsTr("For review") + (view.account.unreviewed ? " (" + view.account.unreviewed + ")" : ""), qsTr("Register"), qsTr("Rules")]
                    currentIndex: view.tab
                    onTabActivated: function(index) { view.tab = index }
                }
                Tk.DataTable {
                    objectName: "bankFeed"
                    visible: view.tab === 0
                    Tk.Flex.grow: 1; Tk.Flex.basis: 0
                    model: view.tab === 0 ? view.rows : []
                    alternatingRows: true
                    rowHeight: Tk.Theme.size.rowLg
                    emptyText: qsTr("Nothing to review. Import a statement to bring in new lines.")
                    columns: [
                        Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                        Tk.TableColumn { key: "payee"; title: qsTr("Payee"); flex: 1 },
                        Tk.TableColumn { key: "memo"; title: qsTr("Memo"); flex: 1; muted: true },
                        Tk.TableColumn { key: "amount"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.3; align: Qt.AlignRight; mono: true
                                         formatter: function(value, row) { return row.amountText } },
                        Tk.TableColumn { key: "suggestion"; title: qsTr("Suggestion"); flex: 1
                                         formatter: function(value, row) { return row.suggestion && row.suggestion.label ? row.suggestion.label : "—" } },
                        Tk.TableColumn {
                            key: "id"; title: ""; width: Tk.Theme.size.tableColumn * 3.2
                            delegate: Tk.Flex {
                                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.xs
                                readonly property var line: parent ? parent.row : null
                                Tk.Button { small: true; variant: "outline"; text: qsTr("Accept"); visible: parent.line !== null && !!parent.line.suggestion && String(parent.line.suggestion.label || "").length > 0
                                            onClicked: view.books.acceptSuggestion(parent.line.id) }
                                Tk.Button { small: true; variant: "ghost"; text: qsTr("Categorize…"); onClicked: categorize.openFor(parent.line) }
                                Tk.IconButton { small: true; iconName: "eye-off"; tooltip: qsTr("Exclude (not a real transaction)"); onClicked: view.books.exclude(parent.line.id) }
                            }
                        }
                    ]
                }
                Tk.DataTable {
                    objectName: "bankRegister"
                    visible: view.tab === 1
                    Tk.Flex.grow: 1; Tk.Flex.basis: 0
                    model: view.tab === 1 ? view.rows : []
                    alternatingRows: true
                    emptyText: qsTr("No transactions in the last twelve months")
                    columns: [
                        Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                        Tk.TableColumn { key: "number"; title: qsTr("No."); width: Tk.Theme.size.tableColumn * 0.8; mono: true },
                        Tk.TableColumn { key: "payee"; title: qsTr("Payee"); flex: 1 },
                        Tk.TableColumn { key: "memo"; title: qsTr("Memo"); flex: 1; muted: true },
                        Tk.TableColumn { key: "paymentText"; title: qsTr("Payment"); width: Tk.Theme.size.tableColumn * 1.2; align: Qt.AlignRight; mono: true },
                        Tk.TableColumn { key: "depositText"; title: qsTr("Deposit"); width: Tk.Theme.size.tableColumn * 1.2; align: Qt.AlignRight; mono: true },
                        Tk.TableColumn { key: "reconciled"; title: "R"; width: Tk.Theme.size.control; formatter: function(value) { return value ? "✓" : "" } },
                        Tk.TableColumn { key: "balanceText"; title: qsTr("Balance"); width: Tk.Theme.size.tableColumn * 1.3; align: Qt.AlignRight; mono: true }
                    ]
                }
                Tk.Flex {
                    visible: view.tab === 2
                    Tk.Flex.grow: 1; Tk.Flex.basis: 0
                    direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
                    Tk.Flex { direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                        Tk.Button { objectName: "bankNewRule"; small: true; variant: "outline"; iconName: "plus"; text: qsTr("New rule"); onClicked: ruleDialog.openFor({}) }
                        Tk.Button { small: true; variant: "ghost"; iconName: "refresh-cw"; text: qsTr("Apply rules to new lines"); onClicked: view.books.applyRules(view.accountId) } }
                    Tk.DataTable {
                        objectName: "bankRules"
                        Tk.Flex.grow: 1; Tk.Flex.basis: 0
                        model: view.tab === 2 ? view.rows : []
                        emptyText: qsTr("Rules categorize recurring lines for you: e.g. payee contains “ADOBE” → Software and Subscriptions.")
                        onActivated: function(index) { ruleDialog.openFor(view.rows[index]) }
                        columns: [
                            Tk.TableColumn { key: "name"; title: qsTr("Rule"); flex: 1 },
                            Tk.TableColumn { key: "payeeContains"; title: qsTr("Payee contains"); flex: 1; mono: true },
                            Tk.TableColumn { key: "accountName"; title: qsTr("Category"); flex: 1 }
                        ]
                    }
                }
            }
            ReconcileView {
                anchors.fill: parent
                visible: view.sessionId > 0
                books: view.books
                sessionId: view.sessionId
                accountName: String(view.account.name || "")
                onDone: view.sessionId = 0
            }
        }
    }

    // ---- dialogs ----
    Dialogs.FileDialog {
        id: importDialog
        title: qsTr("Import a bank statement")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Bank statements (*.csv *.ofx *.qfx *.qif)"), qsTr("All files (*)")]
        onAccepted: {
            const r = view.books.importStatement(view.accountId, selectedFile, ({}))
            if (r.needsMapping) mapping.openFor(selectedFile, r)
            else view.tab = 0
        }
    }
    Tk.Dialog {
        id: mapping
        property url file
        property var columns: []
        title: qsTr("Which column is which?")
        subtitle: qsTr("The statement's columns could not be recognised for certain.")
        primaryText: qsTr("Import")
        secondaryText: qsTr("Cancel")
        function openFor(url, result) { file = url; columns = result.columns || []; open() }
        onAccepted: view.books.importStatement(view.accountId, file, { date: dateCol.currentIndex, amount: amountCol.currentIndex,
                                                                       payee: payeeCol.currentIndex, memo: memoCol.currentIndex - 1 })
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Date"); Tk.ComboBox { id: dateCol; small: true; model: mapping.columns } }
            Tk.PropertyRow { label: qsTr("Amount"); Tk.ComboBox { id: amountCol; small: true; model: mapping.columns } }
            Tk.PropertyRow { label: qsTr("Payee"); Tk.ComboBox { id: payeeCol; small: true; model: mapping.columns } }
            Tk.PropertyRow { label: qsTr("Memo"); Tk.ComboBox { id: memoCol; small: true; model: [qsTr("(none)")].concat(mapping.columns) } }
        }
    }
    Tk.Dialog {
        id: categorize
        property var line: ({})
        property var categories: []
        property var payees: []
        title: qsTr("Categorize")
        subtitle: String(line.date || "") + " · " + String(line.payee || "") + " · " + String(line.amountText || "")
        primaryText: qsTr("Add to the books")
        secondaryText: qsTr("Cancel")
        function openFor(l) {
            line = l
            categories = view.books.accounts(false).filter(function(a) { return a.type !== "Bank" })
            payees = [{ id: 0, name: qsTr("(none)") }].concat(view.books.parties("vendor", false)).concat(view.books.parties("customer", false))
            open()
        }
        onAccepted: view.books.categorize(line.id, { accountId: categories[catBox.currentIndex].id, partyId: payees[payeeBox.currentIndex].id, memo: memoField.text })
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Category"); Tk.ComboBox { id: catBox; small: true; textRole: "fullName"; model: categorize.categories } }
            Tk.PropertyRow { label: qsTr("Payee"); Tk.ComboBox { id: payeeBox; small: true; textRole: "name"; model: categorize.payees } }
            Tk.PropertyRow { label: qsTr("Memo"); Tk.TextField { id: memoField; text: String(categorize.line.memo || "") } }
        }
    }
    Tk.Dialog {
        id: ruleDialog
        property var rule: ({})
        property var categories: []
        title: Number(rule.id || 0) > 0 ? qsTr("Edit rule") : qsTr("New bank rule")
        primaryText: qsTr("Save")
        secondaryText: qsTr("Cancel")
        function openFor(r) {
            rule = r
            categories = view.books.accounts(false).filter(function(a) { return a.type !== "Bank" })
            ruleName.text = String(r.name || ""); ruleContains.text = String(r.payeeContains || "")
            open()
        }
        onAccepted: view.books.saveRule(Object.assign({}, rule, { name: ruleName.text, payeeContains: ruleContains.text,
                                                                  accountId: categories[ruleCat.currentIndex].id, bankAccountId: view.accountId }))
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: ruleName } }
            Tk.PropertyRow { label: qsTr("Payee contains"); Tk.TextField { id: ruleContains; mono: true } }
            Tk.PropertyRow { label: qsTr("Categorize as"); Tk.ComboBox { id: ruleCat; small: true; textRole: "fullName"; model: ruleDialog.categories
                currentIndex: Math.max(0, ruleDialog.categories.map(function(c) { return c.id }).indexOf(ruleDialog.rule.accountId || 0)) } }
        }
    }
    Tk.Dialog {
        id: reconcileStart
        property var target: ({})
        title: qsTr("Reconcile %1").arg(String(target.name || ""))
        subtitle: qsTr("Use the numbers from your bank statement.")
        primaryText: qsTr("Start reconciling")
        secondaryText: qsTr("Cancel")
        function openFor(a) { target = a; statementDate.text = view.books.today; endingBalance.text = ""; open() }
        onAccepted: {
            const r = view.books.startReconciliation(target.id, statementDate.text, endingBalance.text)
            // An unfinished session is resumed rather than refused.
            if (r.ok || r.inProgress) view.sessionId = r.sessionId
        }
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Statement date"); Tk.TextField { id: statementDate; mono: true } }
            Tk.PropertyRow { label: qsTr("Ending balance"); Tk.TextField { id: endingBalance; mono: true; placeholderText: "0.00" } }
            Tk.Button { small: true; variant: "ghost"; iconName: "rotate-ccw"; text: qsTr("Undo the last reconciliation"); onClicked: { view.books.undoLastReconciliation(reconcileStart.target.id); reconcileStart.close() } }
        }
    }
}
