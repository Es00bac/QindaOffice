// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtCharts
import QindaTK as Tk

// One chart drawn from the host's series data ({title, kind, categories,
// series:[{name, values}]}) with QtCharts' ChartView. Loaded only when the
// suite was built with charts (the import fails otherwise, in this file
// alone).
ChartView {
    id: view

    property var data: ({ title: "", kind: 0, categories: [], series: [] })

    title: data.title !== undefined ? data.title : ""
    antialiasing: true
    legend.visible: true
    theme: Tk.Theme.isDark ? ChartView.ChartThemeDark : ChartView.ChartThemeLight
    backgroundColor: Tk.Theme.color.panel

    onDataChanged: rebuild()
    Component.onCompleted: rebuild()

    function rebuild() {
        view.removeAllSeries()
        for (let i = view.axes.length - 1; i >= 0; --i) view.removeAxis(view.axes[i])
        const d = view.data
        if (d === undefined || d.series === undefined || d.series.length === 0) return
        const categories = d.categories
        let maxValue = 0
        for (let s = 0; s < d.series.length; ++s)
            for (let v = 0; v < d.series[s].values.length; ++v) maxValue = Math.max(maxValue, d.series[s].values[v])
        const kind = d.kind
        if (kind === 4) {
            const pie = view.createSeries(ChartView.SeriesTypePie, d.series[0].name)
            for (let v = 0; v < d.series[0].values.length; ++v) pie.append(categories[v], d.series[0].values[v])
            return
        }
        if (kind === 0 || kind === 1) {
            const axisX = Qt.createQmlObject("import QtCharts; BarCategoryAxis {}", view)
            axisX.categories = categories
            const axisY = Qt.createQmlObject("import QtCharts; ValueAxis { min: 0 }", view)
            axisY.max = maxValue * 1.1 + 1
            const bars = view.createSeries(kind === 1 ? ChartView.SeriesTypeStackedBar : ChartView.SeriesTypeBar, "", axisX, axisY)
            for (let s = 0; s < d.series.length; ++s) bars.append(d.series[s].name, d.series[s].values)
            return
        }
        const axisX = Qt.createQmlObject("import QtCharts; ValueAxis { min: 1 }", view)
        axisX.max = Math.max(2, categories.length)
        const axisY = Qt.createQmlObject("import QtCharts; ValueAxis { min: 0 }", view)
        axisY.max = maxValue * 1.1 + 1
        for (let s = 0; s < d.series.length; ++s) {
            const type = kind === 3 ? ChartView.SeriesTypeLine : kind === 5 ? ChartView.SeriesTypeScatter : ChartView.SeriesTypeLine
            const line = view.createSeries(type, d.series[s].name, axisX, axisY)
            for (let v = 0; v < d.series[s].values.length; ++v) line.append(v + 1, d.series[s].values[v])
        }
    }
}
