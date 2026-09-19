// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The chart surface (D-036): a QQuickPaintedItem rendering the
// {title, kind, categories, series:[{name, values}]} map chartSeriesData
// produces. QtCharts' ChartView is not an option — its title rendering
// creates a QWidgetTextControl, which dereferences null under
// QGuiApplication (the whole suite runs one, per D-031), so every chart
// window crashed on creation. The renderer handles the six engine kinds
// with axes, gridlines and a legend, needs no Qt module beyond Gui/Quick,
// and — unlike the ChartView path — is testable offscreen.

#include <QQuickPaintedItem>

#include <QVariantList>
#include <QVariantMap>

namespace QindaOffice::Apps::QindaCalc {

class ChartItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // The chartSeriesData() map; repaints on assignment.
    Q_PROPERTY(QVariantMap chartData READ chartData WRITE setChartData NOTIFY chartDataChanged)
    // The text/axis ink (QML sets Tk.Theme.color.text — the item is the one
    // thing on the window that should follow the theme).
    Q_PROPERTY(QColor inkColor READ inkColor WRITE setInkColor NOTIFY inkColorChanged)
public:
    explicit ChartItem(QQuickItem *parent = nullptr);

    [[nodiscard]] QVariantMap chartData() const { return m_data; }
    void setChartData(const QVariantMap &data);
    [[nodiscard]] QColor inkColor() const { return m_ink; }
    void setInkColor(const QColor &color) { m_ink = color; emit inkColorChanged(); update(); }

    void paint(QPainter *painter) override;

signals:
    void chartDataChanged();
    void inkColorChanged();

private:
    void paintBar(QPainter &painter, const QRectF &plot, bool stacked);
    void paintLines(QPainter &painter, const QRectF &plot, bool fill, bool pointsOnly);

    QVariantMap m_data;
    QColor m_ink{0xe0, 0xe0, 0xe4};
    // Decomposed on assignment (QVariantMap access is not free per frame).
    QString m_title;
    int m_kind = 0;
    QStringList m_categories;
    QStringList m_seriesNames;
    QVector<QVector<double>> m_seriesValues;
    double m_minValue = 0;
    double m_maxValue = 0;
};

} // namespace QindaOffice::Apps::QindaCalc
