// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One headline figure of a report (a ReportKpi map from PlanService.report):
// label, value, detail and, for a share, a meter. `tone` colours the value
// and the icon: good · warning · danger.
Tk.Card {
    id: card

    property var kpi: ({})
    readonly property string tone: String(kpi.tone || "")
    readonly property color toneColor: tone === "danger" ? Tk.Theme.color.danger
                                     : tone === "warning" ? Tk.Theme.color.warning
                                     : tone === "good" ? Tk.Theme.color.success : Tk.Theme.color.text

    objectName: "planKpi_" + String(kpi.id || "")
    implicitWidth: Tk.Theme.size.panelMinWidth
    tooltip: String(kpi.label || "") + ": " + String(kpi.value || "")
    Accessible.role: Accessible.StaticText
    Accessible.name: tooltip

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Overline { title: String(card.kpi.label || "") }
        Tk.Heading {
            objectName: "kpiValue"
            level: 1
            text: String(card.kpi.value || "—")
            color: card.toneColor
            elide: Text.ElideRight
        }
        Tk.Meter {
            visible: Number(card.kpi.percent) >= 0
            from: 0
            to: 100
            value: Math.max(0, Number(card.kpi.percent || 0))
        }
        Tk.Caption {
            text: String(card.kpi.detail || "")
            visible: text.length > 0
            elide: Text.ElideRight
        }
    }
}
