// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Settings ▸ Preferences ▸ QindaBase (also Database Tools ▸ Properties):
// this database's description and currency, where automatic backups go,
// and what the file is. Saved into the database (qb_meta) on Apply.
Tk.Flex {
    id: page

    property var host: null
    readonly property var base: host !== null && host.base !== undefined ? host.base : null
    property var info: ({})

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function load() {
        if (base === null || !base.hasDatabase) return
        info = base.properties()
        symbolField.text = info.currencySymbol || "$"
        decimalsField.value = info.currencyDecimals === undefined ? 2 : info.currencyDecimals
        descriptionField.text = info.description || ""
    }
    function apply() {
        if (base === null || !base.hasDatabase) return
        const r = base.setProperties({ currencySymbol: symbolField.text, currencyDecimals: decimalsField.value,
                                       description: descriptionField.text })
        if (!r.ok && !r.needsSystemTables) problem.text = r.error
    }
    onHostChanged: load()
    Component.onCompleted: load()

    Tk.Notice { visible: page.base === null || !page.base.hasDatabase; variant: "info"; title: qsTr("No database open"); text: qsTr("Open or create a database to edit its properties.") }
    Tk.Notice { id: problem; visible: text.length > 0; variant: "danger" }
    Tk.SectionHeader { title: qsTr("This database") }
    Tk.PropertyRow { label: qsTr("Description"); alignTop: true; Tk.TextArea { id: descriptionField; objectName: "prefDescription"; rows: 2 } }
    Tk.PropertyRow {
        label: qsTr("Currency symbol")
        hint: qsTr("Shown before amounts in Currency fields")
        Tk.TextField { id: symbolField; objectName: "prefCurrencySymbol"; mono: true }
    }
    Tk.PropertyRow {
        label: qsTr("Decimal places")
        hint: qsTr("Fixed once a Currency field holds amounts")
        Tk.NumberField { id: decimalsField; objectName: "prefCurrencyDecimals"; from: 0; to: 4; decimals: 0; stepSize: 1 }
    }
    Tk.SectionHeader { title: qsTr("File") }
    Tk.KeyValue { key: qsTr("Location"); value: page.info.path || qsTr("not saved yet") }
    Tk.KeyValue { key: qsTr("Format"); value: page.info.isQBase ? qsTr("QindaBase (.qbase)") : qsTr("SQLite") }
    Tk.KeyValue { key: qsTr("Size"); value: Math.max(1, Math.round((page.info.sizeBytes || 0) / 1024)) + " KB" }
    Tk.KeyValue { key: qsTr("Automatic backups"); value: page.info.backupFolder || ""; mono: true }
}
