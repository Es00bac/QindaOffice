// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One headline number on the dashboard: an overline label, the value, a
// caption with context. Clicking runs `action` (e.g. open the overdue list).
Tk.Card {
    id: card

    property string label: ""
    property string value: ""
    property string caption: ""
    property string iconName: ""
    property string tone: "default"      // default | good | warning | danger

    interactive: true
    implicitWidth: Tk.Theme.size.panelMinWidth
    tooltip: label + ": " + value
    Accessible.role: Accessible.Button
    Accessible.name: tooltip

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: card.label; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Icon {
                visible: card.iconName.length > 0
                name: card.iconName
                size: Tk.Theme.size.iconLg
                color: card.tone === "danger" ? Tk.Theme.color.danger : card.tone === "warning" ? Tk.Theme.color.warning
                     : card.tone === "good" ? Tk.Theme.color.success : Tk.Theme.color.accent
            }
        }
        Tk.Heading { objectName: "kpiValue"; level: 1; text: card.value; elide: Text.ElideRight }
        Tk.Caption { text: card.caption; elide: Text.ElideRight }
    }
}
