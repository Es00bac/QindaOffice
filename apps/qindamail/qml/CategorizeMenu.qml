// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Categorize menu (M81, D-090): the master list of the item's
// account with the assigned ones checked; picking one adds or removes it.
// Below: All Categories… (the Color Categories dialog) and Clear All
// Categories. `kind` is message | event | task | contact.
//
// AGENT-NOTE: the categories are user data, so they are menu rows here, not
// ActionEntries — the menu tree and the panel export carry the fixed
// commands (Categorize, All Categories, Clear All Categories) only.
// AGENT-NOTE (M89, QindaTK D-323): a checkable Tk.MenuItem keeps its icon
// and frames it when checked, so every category shows its tag and the
// assigned ones are framed (before M85 the check mark replaced the icon).
// The tag is drawn in theme colours: Tk.MenuItem has no per-item icon
// colour, so the category's own colour shows in its chips and in Color
// Categories, not here (a QindaTK gap, reported in D-097).
Tk.Menu {
    id: menu

    property MailService service: null
    property string kind: "message"
    property var ids: []
    property string accountId: ""
    readonly property var categories: service !== null && service.organizer.revision >= 0
                                      ? service.organizer.categoriesFor(accountId) : []
    property var assigned: []

    signal allCategoriesRequested()

    objectName: "categorizeMenu"
    title: qsTr("Categorize")

    function openFor(kind, ids, accountId) {
        menu.kind = kind
        menu.ids = ids
        menu.accountId = accountId
        menu.assigned = ids.length > 0 ? service.organizer.categoriesOf(kind, ids[0]) : []
        popup()
    }
    function has(name) {
        return assigned.some(function(n) { return String(n).toLowerCase() === String(name).toLowerCase() })
    }

    Repeater {
        model: menu.categories
        delegate: Tk.MenuItem {
            required property var modelData
            objectName: "categorize_" + String(modelData.name)
            text: String(modelData.name)
            checkable: true
            checked: menu.has(modelData.name)
            iconName: "tag"
            onTriggered: menu.service.organizer.toggleCategory(menu.kind, menu.ids, String(modelData.name), !menu.has(modelData.name))
        }
    }
    Tk.MenuSeparator { }
    Tk.MenuItem {
        objectName: "categorizeAll"
        text: qsTr("All Categories…")
        iconName: "sliders-horizontal"
        onTriggered: menu.allCategoriesRequested()
    }
    Tk.MenuItem {
        objectName: "categorizeClear"
        text: qsTr("Clear All Categories")
        iconName: "eraser"
        enabled: menu.assigned.length > 0
        onTriggered: menu.service.organizer.setCategories(menu.kind, menu.ids, [])
    }
}
