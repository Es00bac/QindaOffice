// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Bank and payment connections for this company (D-048): Mercury brings
// posted bank transactions into the review feed; Stripe brings charges at
// gross, its fees, refunds and payouts through a clearing account, and
// settles invoices paid through a payment link. Tokens go to the keyring
// through the service — this dialog never keeps one after Connect.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property var connections: []
    property var mercuryAccounts: []
    property var links: []
    property var bankAccounts: []
    property var stripe: ({})
    property string mercuryError: ""
    property string stripeError: ""
    readonly property var mercury: connections.find(function(c) { return c.provider === "mercury" }) || ({ status: "disconnected" })
    readonly property var stripeState: connections.find(function(c) { return c.provider === "stripe" }) || ({ status: "disconnected" })

    objectName: "connectionsDialog"
    title: qsTr("Bank and payment connections")
    subtitle: books !== null ? books.companyName : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.7
    primaryText: qsTr("Done")
    secondaryText: ""
    tertiaryText: books !== null && books.syncingConnections ? qsTr("Syncing…") : qsTr("Sync now")

    function refresh() {
        connections = books.connections()
        links = mercury.status === "connected" ? books.mercuryLinks() : []
        bankAccounts = [{ id: 0, name: qsTr("Create a new bank account") }].concat(books.bankAccounts())
        stripe = stripeState.status === "connected" ? books.stripeSettings() : ({})
    }
    function openDialog() { mercuryError = ""; stripeError = ""; refresh(); open() }
    function statusVariant(s) { return s === "connected" ? "success" : s === "error" || s === "needsToken" ? "danger" : s === "syncing" ? "info" : "default" }
    function statusText(s) {
        return s === "connected" ? qsTr("Connected") : s === "syncing" ? qsTr("Syncing…") : s === "error" ? qsTr("Error")
             : s === "needsToken" ? qsTr("Needs a new key") : qsTr("Not connected")
    }
    onTertiary: { books.syncConnections(); open() }
    Connections {
        target: dialog.books
        function onChanged() { if (dialog.opened) dialog.refresh() }
        function onConnectionsChanged() { if (dialog.opened) dialog.refresh() }
        function onConnectionSyncFinished(requestId, result) {
            dialog.refresh()
            if (!result.ok) dialog.mercuryError = (result.errors || []).join("\n")
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        // ---- Mercury ----
        Tk.Card {
            objectName: "connectionMercury"
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                    Tk.Icon { name: "landmark"; size: Tk.Theme.size.iconXl; color: Tk.Theme.color.accent }
                    Tk.Flex { direction: Tk.Flex.Column; Tk.Flex.grow: 1; Tk.Flex.basis: 0
                        Tk.Label { text: qsTr("Mercury"); font.weight: Font.DemiBold }
                        Tk.Caption { text: String(dialog.mercury.summary || qsTr("Posted transactions arrive in Banking ▸ For review.")) } }
                    Tk.Badge { text: dialog.statusText(dialog.mercury.status); variant: dialog.statusVariant(dialog.mercury.status) }
                }
                Tk.Notice { visible: dialog.mercuryError.length > 0 || String(dialog.mercury.lastError || "").length > 0; variant: "danger"
                            title: qsTr("Mercury"); text: dialog.mercuryError || String(dialog.mercury.lastError || "") }
                Tk.Flex {
                    visible: dialog.mercury.status !== "connected"
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                    Tk.TextField { id: mercuryToken; objectName: "mercuryToken"; Tk.Flex.grow: 1; echoMode: TextInput.Password
                                   placeholderText: qsTr("Mercury token — Settings ▸ Tokens ▸ Create ▸ Read Only") }
                    Tk.Button {
                        objectName: "mercuryConnect"; variant: "accent"; text: qsTr("Connect")
                        available: mercuryToken.text.trim().length > 0
                        onClicked: {
                            const r = dialog.books.connectMercury(mercuryToken.text.trim())
                            mercuryToken.text = ""
                            dialog.mercuryError = r.ok ? "" : String(r.error || "")
                            dialog.mercuryAccounts = r.accounts || []
                            dialog.refresh()
                        }
                    }
                }
                Tk.Overline { visible: dialog.mercury.status === "connected"; title: qsTr("Accounts") }
                Repeater {
                    model: dialog.mercury.status === "connected" ? dialog.links : []
                    delegate: Tk.Flex {
                        id: link
                        required property var modelData
                        objectName: "mercuryLink_" + modelData.mercuryAccountId
                        direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                        Tk.Label { text: String(link.modelData.name || "") + (link.modelData.lastFour ? " ••" + link.modelData.lastFour : ""); Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight }
                        Tk.Caption { text: qsTr("goes to") }
                        Tk.ComboBox {
                            small: true
                            textRole: "name"
                            // "Not imported" unlinks (-1); "Create a new bank account" (0) makes
                            // "Mercury Checking ••1234"; any other id links an existing account.
                            model: [{ id: -1, name: qsTr("Not imported") }].concat(dialog.bankAccounts)
                            currentIndex: link.modelData.linked ? Math.max(0, model.map(function(a) { return a.id }).indexOf(link.modelData.booksAccountId)) : 0
                            onActivated: function(i) { dialog.books.linkMercuryAccount(link.modelData.mercuryAccountId, model[i].id); dialog.refresh() }
                        }
                    }
                }
                Tk.Flex {
                    visible: dialog.mercury.status === "connected"
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                    Tk.Caption { text: dialog.mercury.lastSync ? qsTr("Last synced %1").arg(dialog.mercury.lastSync) : qsTr("Not synced yet"); Tk.Flex.grow: 1 }
                    Tk.Button { small: true; variant: "ghost"; text: qsTr("Disconnect"); onClicked: { dialog.books.disconnect("mercury"); dialog.refresh() } }
                }
            }
        }

        // ---- Stripe ----
        Tk.Card {
            objectName: "connectionStripe"
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                    Tk.Icon { name: "credit-card"; size: Tk.Theme.size.iconXl; color: Tk.Theme.color.accent }
                    Tk.Flex { direction: Tk.Flex.Column; Tk.Flex.grow: 1; Tk.Flex.basis: 0
                        Tk.Label { text: qsTr("Stripe"); font.weight: Font.DemiBold }
                        Tk.Caption { text: String(dialog.stripeState.summary || qsTr("Charges, fees, refunds and payouts, and invoices paid online.")) } }
                    Tk.Badge { text: dialog.statusText(dialog.stripeState.status); variant: dialog.statusVariant(dialog.stripeState.status) }
                }
                Tk.Notice { visible: dialog.stripeError.length > 0 || String(dialog.stripeState.lastError || "").length > 0; variant: "danger"
                            title: qsTr("Stripe"); text: dialog.stripeError || String(dialog.stripeState.lastError || "") }
                Tk.Flex {
                    visible: dialog.stripeState.status !== "connected"
                    direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                    Tk.TextField { id: stripeKey; objectName: "stripeKey"; Tk.Flex.grow: 1; echoMode: TextInput.Password
                                   placeholderText: qsTr("Stripe restricted key (rk_live_…) — read Balance, Charges, Refunds, Payouts") }
                    Tk.Button {
                        objectName: "stripeConnect"; variant: "accent"; text: qsTr("Connect")
                        available: stripeKey.text.trim().length > 0
                        onClicked: {
                            const r = dialog.books.connectStripe(stripeKey.text.trim())
                            stripeKey.text = ""
                            dialog.stripeError = r.ok ? "" : String(r.error || "")
                            dialog.refresh()
                        }
                    }
                }
                Tk.Flex {
                    visible: dialog.stripeState.status === "connected"
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.PropertyRow {
                        label: qsTr("Payouts go to")
                        Tk.ComboBox {
                            objectName: "stripePayoutAccount"; small: true; textRole: "name"
                            model: dialog.bankAccounts.slice(1)
                            currentIndex: Math.max(0, model.map(function(a) { return a.id }).indexOf(dialog.stripe.payoutAccountId || 0))
                            onActivated: function(i) { dialog.books.saveStripeSettings(Object.assign({}, dialog.stripe, { payoutAccountId: model[i].id })); dialog.refresh() }
                        }
                    }
                    Tk.Switch { text: qsTr("Book Stripe fees automatically"); checked: dialog.stripe.autoPostFees !== false
                                onToggled: dialog.books.saveStripeSettings(Object.assign({}, dialog.stripe, { autoPostFees: checked })) }
                    Tk.Switch { text: qsTr("Match payouts to the bank deposit automatically"); checked: dialog.stripe.autoPostPayouts !== false
                                onToggled: dialog.books.saveStripeSettings(Object.assign({}, dialog.stripe, { autoPostPayouts: checked })) }
                    Tk.Switch { text: qsTr("Mark invoices paid when their payment link is paid"); checked: dialog.stripe.autoApplyInvoices !== false
                                onToggled: dialog.books.saveStripeSettings(Object.assign({}, dialog.stripe, { autoApplyInvoices: checked })) }
                    Tk.Flex {
                        direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                        Tk.Caption { text: dialog.stripeState.lastSync ? qsTr("Last synced %1").arg(dialog.stripeState.lastSync) : qsTr("Not synced yet"); Tk.Flex.grow: 1 }
                        Tk.Button { small: true; variant: "ghost"; text: qsTr("Disconnect"); onClicked: { dialog.books.disconnect("stripe"); dialog.refresh() } }
                    }
                }
            }
        }
        Tk.Caption {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Keys are kept in your desktop keyring for this company only — never in the company file. Imported lines always pass through Banking ▸ For review unless you let fees and payouts post automatically.")
        }
    }
}
