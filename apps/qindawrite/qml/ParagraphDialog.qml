// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format ▸ Paragraph (M70, D-079): Word's Paragraph dialog. Indents and
// Spacing — alignment, outline level, left/right indents and the special
// first-line or hanging indent (inches or centimetres; the host speaks
// 96-dpi document px), spacing before and after in points with Auto, line
// spacing (single, 1.5, double, at least, exactly, multiple) and "don't add
// space between paragraphs of the same style"; Line and Page Breaks —
// widow/orphan control, keep with next, keep lines together, page break
// before. Only what the user touches is applied (`changed`), so a
// selection of differently formatted paragraphs keeps the rest. Tabs…
// opens the Tabs dialog (TabsDialog.qml).
Tk.Dialog {
    id: dialog
    signal paragraphApplied(var answer)
    signal tabsRequested()
    property var changed: ({})
    property bool loading: false
    readonly property real pxPerUnit: units.currentIndex === 0 ? 96 : 96 / 2.54
    readonly property string unitSuffix: units.currentIndex === 0 ? qsTr(" in") : qsTr(" cm")
    readonly property var alignments: ["left", "center", "right", "justify"]
    readonly property var specials: ["none", "firstLine", "hanging"]
    readonly property var lineRules: ["single", "oneHalf", "double", "atLeast", "exactly", "multiple"]
    readonly property bool lineInPoints: lineRule.currentIndex === 3 || lineRule.currentIndex === 4
    objectName: "paragraphDialog"
    title: qsTr("Paragraph")
    dialogWidth: 520
    primaryText: qsTr("OK")

    function mark(key) {
        if (loading) return
        const next = Object.assign({}, changed)
        next[key] = true
        changed = next
    }
    function load(info) {
        loading = true
        changed = ({})
        pages.currentIndex = 0
        alignment.currentIndex = Math.max(0, alignments.indexOf(info.alignment))
        outline.currentIndex = info.outline >= 9 ? 0 : info.outline + 1
        left.value = info.left / pxPerUnit
        right.value = info.right / pxPerUnit
        special.currentIndex = Math.max(0, specials.indexOf(info.special))
        by.value = info.by / pxPerUnit
        before.value = info.before
        beforeAuto.checked = info.beforeAuto
        after.value = info.after
        afterAuto.checked = info.afterAuto
        lineRule.currentIndex = Math.max(0, lineRules.indexOf(info.lineRule))
        lineAt.value = info.lineAt
        contextual.checked = info.contextual
        widow.checked = info.widow
        keepNext.checked = info.keepNext
        keepLines.checked = info.keepLines
        pageBreak.checked = info.pageBreakBefore
        loading = false
    }
    function answer() {
        return {
            changed: Object.keys(changed),
            alignment: alignments[alignment.currentIndex],
            outline: outline.currentIndex === 0 ? 9 : outline.currentIndex - 1,
            left: left.value * pxPerUnit, right: right.value * pxPerUnit,
            special: specials[special.currentIndex], by: by.value * pxPerUnit,
            before: before.value, beforeAuto: beforeAuto.checked,
            after: after.value, afterAuto: afterAuto.checked,
            lineRule: lineRules[lineRule.currentIndex], lineAt: lineAt.value,
            contextual: contextual.checked, widow: widow.checked, keepNext: keepNext.checked,
            keepLines: keepLines.checked, pageBreakBefore: pageBreak.checked
        }
    }
    onAccepted: if (Object.keys(changed).length > 0) dialog.paragraphApplied(answer())

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            id: pages
            objectName: "paragraphPages"
            model: [qsTr("Indents and Spacing"), qsTr("Line and Page Breaks")]
        }
        Tk.Flex {
            visible: pages.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyGroup {
                title: qsTr("General")
                collapsible: false
                Tk.PropertyRow {
                    label: qsTr("Alignment")
                    Tk.ComboBox {
                        id: alignment
                        objectName: "paragraphAlignment"
                        model: [qsTr("Left"), qsTr("Centered"), qsTr("Right"), qsTr("Justified")]
                        onActivated: dialog.mark("alignment")
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Outline level")
                    Tk.ComboBox {
                        id: outline
                        objectName: "paragraphOutline"
                        model: [qsTr("Body Text"), qsTr("Level 1"), qsTr("Level 2"), qsTr("Level 3"), qsTr("Level 4"),
                                qsTr("Level 5"), qsTr("Level 6"), qsTr("Level 7"), qsTr("Level 8"), qsTr("Level 9")]
                        onActivated: dialog.mark("outline")
                    }
                }
            }
            Tk.PropertyGroup {
                title: qsTr("Indentation")
                collapsible: false
                Tk.PropertyRow {
                    label: qsTr("Units")
                    Tk.Segmented {
                        id: units
                        objectName: "paragraphUnits"
                        model: [qsTr("Inches"), qsTr("Centimetres")]
                        property real previous: 96
                        onCurrentIndexChanged: {
                            const now = currentIndex === 0 ? 96 : 96 / 2.54
                            const keep = dialog.loading
                            dialog.loading = true
                            for (const f of [left, right, by]) f.value = f.value * previous / now
                            dialog.loading = keep
                            previous = now
                        }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Left / right")
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        Tk.NumberField {
                            id: left; objectName: "paragraphLeft"
                            from: -22; to: 22; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix
                            onValueModified: function(v) { value = v; dialog.mark("left") }
                        }
                        Tk.NumberField {
                            id: right; objectName: "paragraphRight"
                            from: -22; to: 22; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix
                            onValueModified: function(v) { value = v; dialog.mark("right") }
                        }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Special / by")
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        Tk.ComboBox {
                            id: special
                            objectName: "paragraphSpecial"
                            model: [qsTr("(none)"), qsTr("First line"), qsTr("Hanging")]
                            onActivated: dialog.mark("special")
                        }
                        Tk.NumberField {
                            id: by; objectName: "paragraphBy"
                            enabled: special.currentIndex > 0
                            from: 0; to: 22; stepSize: 0.1; decimals: 2; suffix: dialog.unitSuffix
                            onValueModified: function(v) { value = v; dialog.mark("by") }
                        }
                    }
                }
            }
            Tk.PropertyGroup {
                title: qsTr("Spacing")
                collapsible: false
                Tk.PropertyRow {
                    label: qsTr("Before")
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        align: Tk.Flex.Center
                        Tk.NumberField {
                            id: before; objectName: "paragraphBefore"
                            enabled: !beforeAuto.checked
                            from: 0; to: 1584; stepSize: 6; decimals: 1; suffix: qsTr(" pt")
                            onValueModified: function(v) { value = v; dialog.mark("before") }
                        }
                        Tk.CheckBox { id: beforeAuto; objectName: "paragraphBeforeAuto"; text: qsTr("Auto"); onToggled: dialog.mark("beforeAuto") }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("After")
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        align: Tk.Flex.Center
                        Tk.NumberField {
                            id: after; objectName: "paragraphAfter"
                            enabled: !afterAuto.checked
                            from: 0; to: 1584; stepSize: 6; decimals: 1; suffix: qsTr(" pt")
                            onValueModified: function(v) { value = v; dialog.mark("after") }
                        }
                        Tk.CheckBox { id: afterAuto; objectName: "paragraphAfterAuto"; text: qsTr("Auto"); onToggled: dialog.mark("afterAuto") }
                    }
                }
                Tk.PropertyRow {
                    label: qsTr("Line spacing / at")
                    Tk.Flex {
                        gap: Tk.Theme.space.sm
                        Tk.ComboBox {
                            id: lineRule
                            objectName: "paragraphLineRule"
                            model: [qsTr("Single"), qsTr("1.5 lines"), qsTr("Double"), qsTr("At least"), qsTr("Exactly"),
                                    qsTr("Multiple")]
                            onActivated: function(index) {
                                const defaults = [1, 1.5, 2, 12, 12, 1.15]
                                lineAt.value = defaults[index]
                                dialog.mark("lineRule")
                                dialog.mark("lineAt")
                            }
                        }
                        Tk.NumberField {
                            id: lineAt; objectName: "paragraphLineAt"
                            enabled: lineRule.currentIndex >= 3
                            // Constant bounds: points (at least, exactly) or a multiple.
                            from: 0; to: 1584; stepSize: 0.25; decimals: 2
                            suffix: dialog.lineInPoints ? qsTr(" pt") : qsTr(" ×")
                            onValueModified: function(v) { value = v; dialog.mark("lineAt") }
                        }
                    }
                }
                Tk.CheckBox {
                    id: contextual
                    objectName: "paragraphContextual"
                    text: qsTr("Don't add space between paragraphs of the same style")
                    onToggled: dialog.mark("contextual")
                }
            }
        }
        Tk.Flex {
            visible: pages.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.PropertyGroup {
                title: qsTr("Pagination")
                collapsible: false
                Tk.CheckBox { id: widow; objectName: "paragraphWidow"; text: qsTr("Widow/Orphan control"); onToggled: dialog.mark("widow") }
                Tk.CheckBox { id: keepNext; objectName: "paragraphKeepNext"; text: qsTr("Keep with next"); onToggled: dialog.mark("keepNext") }
                Tk.CheckBox { id: keepLines; objectName: "paragraphKeepLines"; text: qsTr("Keep lines together"); onToggled: dialog.mark("keepLines") }
                Tk.CheckBox { id: pageBreak; objectName: "paragraphPageBreak"; text: qsTr("Page break before"); onToggled: dialog.mark("pageBreakBefore") }
            }
        }
        Tk.Flex {
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "paragraphTabs"
                text: qsTr("Tabs…")
                onClicked: { dialog.reject(); dialog.tabsRequested() }
            }
        }
    }
}
