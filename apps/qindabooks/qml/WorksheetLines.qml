// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// A tax worksheet as the publication prints it (M71, D-081): line number,
// the line's words, its value — the Pub 15-T Worksheet 1A, DR 1098, the
// Form 1040-ES and Schedule SE worksheets behind a paycheck or an
// estimate. `lines` is [{line, text, value}] from PayrollService; the text
// wraps so every word of the publication's line stays readable.
Tk.Flex {
    id: sheet

    property var lines: []

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Repeater {
        model: sheet.lines
        delegate: Tk.Flex {
            id: row
            required property var modelData
            required property int index
            objectName: "worksheetLine_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Start
            gap: Tk.Theme.space.sm
            Tk.Mono { text: String(row.modelData.line || ""); muted: true; Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.6; Tk.Flex.shrink: 0 }
            Tk.Label { text: String(row.modelData.text || ""); wrapMode: Text.Wrap; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Mono {
                text: String(row.modelData.value || "")
                horizontalAlignment: Text.AlignRight
                Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.3
                Tk.Flex.shrink: 0
            }
        }
    }
}
