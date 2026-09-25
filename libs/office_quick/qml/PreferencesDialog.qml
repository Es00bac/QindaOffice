// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// Settings ▸ Preferences… (D-014: every control here is READ by something).
// General: chrome, zoom, restore, density, theme preset. Documents:
// auto-save and the document font. A third page comes from the app
// (host.preferencesPage, a QML file whose root has `function apply()`).
// Values are written to the Preferences store on OK.
Tk.Dialog {
    id: dialog

    property DocumentHost host: null
    readonly property bool hasAppPage: host !== null && host.preferencesPage.toString().length > 0

    title: qsTr("Preferences")
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    dialogWidth: 520

    onOpened: {
        showToolBars.checked = Preferences.showToolBars
        ribbonCompact.checked = Preferences.ribbonCompact
        showStatusBar.checked = Preferences.showStatusBar
        defaultZoom.value = Preferences.defaultZoomPercent
        restorePrevious.checked = host !== null && host.restorePreviousDocuments()
        density.currentIndex = Preferences.density === "comfortable" ? 1 : 0
        preset.currentIndex = Math.max(0, presetIds.indexOf(Preferences.themePreset))
        autoSave.value = Preferences.autoSaveMinutes
        useThemeFont.checked = Preferences.documentFontFamily.length === 0
        fontFamily.currentIndex = Math.max(0, fontFamily.model.indexOf(Preferences.documentFontFamily))
        fontSize.value = Preferences.documentFontSize > 0 ? Preferences.documentFontSize : 11
        tabs.currentIndex = 0
    }
    onAccepted: {
        Preferences.showToolBars = showToolBars.checked
        Preferences.ribbonCompact = ribbonCompact.checked
        Preferences.showStatusBar = showStatusBar.checked
        Preferences.defaultZoomPercent = defaultZoom.value
        Preferences.density = density.currentIndex === 1 ? "comfortable" : "compact"
        Preferences.themePreset = presetIds[preset.currentIndex]
        Preferences.autoSaveMinutes = autoSave.value
        Preferences.documentFontFamily = useThemeFont.checked ? "" : fontFamily.currentText
        Preferences.documentFontSize = useThemeFont.checked ? 0 : fontSize.value
        if (host !== null && host.restorePreviousDocumentsAvailable()) {
            host.setRestorePreviousDocuments(restorePrevious.checked)
        }
        if (appPage.item !== null && appPage.item.apply !== undefined) {
            appPage.item.apply()
        }
    }

    readonly property var presetIds: ["sloom-dark", "sloom-light", "graphite"]

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.TabStrip {
            id: tabs
            objectName: "preferencesTabs"
            model: dialog.hasAppPage ? [qsTr("General"), qsTr("Documents"), dialog.host.appDisplayName]
                                     : [qsTr("General"), qsTr("Documents")]
        }

        // ---- General ----
        Tk.Flex {
            visible: tabs.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Ribbon"); Tk.Switch { id: showToolBars; objectName: "prefShowToolBars"; text: qsTr("Show the ribbon") } }
            Tk.PropertyRow { label: qsTr("Ribbon layout"); Tk.Switch { id: ribbonCompact; objectName: "prefRibbonCompact"; text: qsTr("Compact: one row per group") } }
            Tk.PropertyRow { label: qsTr("Status bar"); Tk.Switch { id: showStatusBar; objectName: "prefShowStatusBar"; text: qsTr("Show status bar") } }
            Tk.PropertyRow { label: qsTr("New windows"); Tk.NumberField { id: defaultZoom; objectName: "prefDefaultZoom"; from: 25; to: 400; stepSize: 10; suffix: " %"; label: qsTr("Zoom") } }
            Tk.PropertyRow {
                label: qsTr("Session")
                Tk.Switch {
                    id: restorePrevious
                    objectName: "prefRestorePrevious"
                    text: qsTr("Reopen the previous session's documents")
                    enabled: dialog.host !== null && dialog.host.restorePreviousDocumentsAvailable()
                }
            }
            Tk.Divider { }
            Tk.PropertyRow { label: qsTr("Density"); Tk.ComboBox { id: density; objectName: "prefDensity"; model: [qsTr("Compact"), qsTr("Comfortable")] } }
            Tk.PropertyRow {
                label: qsTr("Theme")
                hint: qsTr("Used when the QindaQt desktop's theme is not available.")
                Tk.ComboBox { id: preset; objectName: "prefThemePreset"; model: ["Sloom dark", "Sloom light", "Graphite"] }
            }
        }

        // ---- Documents ----
        Tk.Flex {
            visible: tabs.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Auto-save")
                hint: qsTr("Rewrites documents that already have a file; 0 turns it off. Unsaved documents are protected by crash recovery, which is always on.")
                Tk.NumberField { id: autoSave; objectName: "prefAutoSave"; from: 0; to: 120; suffix: qsTr(" min") }
            }
            Tk.Divider { }
            Tk.PropertyRow { label: qsTr("Document font"); Tk.Switch { id: useThemeFont; objectName: "prefUseThemeFont"; text: qsTr("Use the application's default font") } }
            Tk.PropertyRow { label: qsTr("Family"); Tk.ComboBox { id: fontFamily; objectName: "prefFontFamily"; model: Qt.fontFamilies(); enabled: !useThemeFont.checked } }
            Tk.PropertyRow { label: qsTr("Size"); Tk.NumberField { id: fontSize; objectName: "prefFontSize"; from: 6; to: 72; suffix: " pt"; enabled: !useThemeFont.checked } }
        }

        // ---- App page ----
        Loader {
            id: appPage
            objectName: "prefAppPage"
            visible: tabs.currentIndex === 2 && dialog.hasAppPage
            active: dialog.hasAppPage
            source: dialog.hasAppPage ? dialog.host.preferencesPage : ""
        }
    }
}
