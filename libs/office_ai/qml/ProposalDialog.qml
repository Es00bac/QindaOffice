// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Original vs proposal for an inline AI action, with Apply / Regenerate /
// Discard (conversion plan §3.8). The proposal is editable: what Apply
// hands back is what the user sees, edits included.
Tk.Dialog {
    id: dialog

    property string original: ""
    property string proposal: ""
    // Apply hands back the (possibly edited) proposal. (Not `applied`:
    // T.Dialog already owns a signal of that name.)
    signal proposalApplied(string text)
    signal regenerate()

    objectName: "proposalDialog"
    title: qsTr("AI proposal")
    dialogWidth: 720
    primaryText: qsTr("Apply")
    secondaryText: qsTr("Discard")
    tertiaryText: qsTr("Regenerate")
    onAccepted: dialog.proposalApplied(proposalArea.text)
    // AGENT-GUARD: Regenerate must not accept — accepting applies, which
    // replaced the selection with exactly the text the user asked to redo.
    onTertiary: { dialog.regenerate(); dialog.close() }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Overline { text: qsTr("Original") }
            Tk.Scroll {
                implicitHeight: 220
                Tk.TextArea { objectName: "proposalOriginal"; width: parent.width; text: dialog.original; readOnly: true; rows: 8 }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Overline { text: qsTr("Proposal") }
            Tk.Scroll {
                implicitHeight: 220
                Tk.TextArea { id: proposalArea; objectName: "proposalText"; width: parent.width; text: dialog.proposal; rows: 8 }
            }
        }
    }
}
