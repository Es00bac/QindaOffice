// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_dialog.h"

#ifdef QQO_WITH_CHARTS

#include "workbook.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QCategoryAxis>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QPieSeries>
#include <QPushButton>
#include <QScatterSeries>
#include <QValueAxis>

namespace QindaOffice::Apps::QindaCalc {

namespace {

struct SeriesData {
    QStringList categories;
    QList<QPair<QString, QVector<double>>> series;
};

// Reads the range grid: first row (headerH) and/or first column (headerV)
// name the series/categories; numeric body cells fill the values.
SeriesData readSeries(const CalcEng::ChartSpec &spec, const CalcEng::Workbook &wb)
{
    SeriesData out;
    const CalcEng::CellRange range = CalcEng::parseChartRange(spec.rangeText);
    if (!range.isValid() || spec.sheet < 0 || spec.sheet >= wb.sheetCount()) {
        return out;
    }
    const int bodyTop = range.topLeft.row + (spec.headerH ? 1 : 0);
    const int bodyLeft = range.topLeft.col + (spec.headerV ? 1 : 0);
    const int cols = range.bottomRight.col - bodyLeft + 1;
    const int rows = range.bottomRight.row - bodyTop + 1;
    for (int c = 0; c < cols; ++c) {
        QString name = spec.headerH
            ? CalcEng::toText(wb.cellValue(spec.sheet, range.topLeft.row, bodyLeft + c))
            : QString();
        if (name.isEmpty()) {
            name = QStringLiteral("Series %1").arg(c + 1);
        }
        QVector<double> values;
        for (int r = 0; r < rows; ++r) {
            const CalcEng::Value v = wb.cellValue(spec.sheet, bodyTop + r, bodyLeft + c);
            values.append(CalcEng::toNumber(v).value_or(0.0));
        }
        out.series.append({name, values});
    }
    for (int r = 0; r < rows; ++r) {
        QString category = spec.headerV
            ? CalcEng::toText(wb.cellValue(spec.sheet, bodyTop + r, range.topLeft.col))
            : QString();
        if (category.isEmpty()) {
            category = QStringLiteral("%1").arg(r + 1);
        }
        out.categories.append(category);
    }
    return out;
}

QChart *buildChart(const CalcEng::ChartSpec &spec, const CalcEng::Workbook &wb, QObject *parent)
{
    auto *chart = new QChart;
    chart->setTitle(spec.title);
    const SeriesData data = readSeries(spec, wb);
    if (data.series.isEmpty()) {
        return chart;
    }
    switch (spec.kind) {
    case CalcEng::ChartSpec::Kind::Bar:
    case CalcEng::ChartSpec::Kind::StackedBar: {
        auto *series = new QBarSeries;
        for (const auto &[name, values] : data.series) {
            auto *set = new QBarSet(name);
            for (double v : values) {
                *set << v;
            }
            series->append(set);
        }
        chart->addSeries(series);
        auto *axis = new QBarCategoryAxis;
        axis->append(data.categories);
        chart->createDefaultAxes();
        chart->setAxisX(axis, series);
        break;
    }
    case CalcEng::ChartSpec::Kind::Line:
    case CalcEng::ChartSpec::Kind::Area: {
        for (const auto &[name, values] : data.series) {
            auto *line = new QLineSeries;
            line->setName(name);
            for (int i = 0; i < values.size() && i < data.categories.size(); ++i) {
                line->append(double(i), values.at(i));
            }
            chart->addSeries(line);
        }
        chart->createDefaultAxes();
        break;
    }
    case CalcEng::ChartSpec::Kind::Scatter: {
        for (const auto &[name, values] : data.series) {
            auto *scatter = new QScatterSeries;
            scatter->setName(name);
            for (int i = 0; i < values.size(); ++i) {
                scatter->append(double(i), values.at(i));
            }
            chart->addSeries(scatter);
        }
        chart->createDefaultAxes();
        break;
    }
    case CalcEng::ChartSpec::Kind::Pie: {
        auto *pie = new QPieSeries;
        const QVector<double> &values = data.series.first().second;
        for (int i = 0; i < values.size() && i < data.categories.size(); ++i) {
            pie->append(data.categories.at(i), values.at(i));
        }
        chart->addSeries(pie);
        break;
    }
    }
    chart->legend()->setVisible(data.series.size() > 1);
    Q_UNUSED(parent);
    return chart;
}

} // namespace

QWidget *chartWidgetFor(const CalcEng::ChartSpec &spec, const CalcEng::Workbook &wb, QWidget *parent)
{
    auto *view = new QChartView(buildChart(spec, wb, parent));
    view->setRenderHint(QPainter::Antialiasing);
    return view;
}

ChartDialog::ChartDialog(CalcEng::Workbook *wb, const QString &initialRange, QWidget *parent)
    : QDialog(parent)
    , m_workbook(wb)
    , m_kind(new QComboBox(this))
    , m_title(new QLineEdit(this))
    , m_range(new QLineEdit(initialRange, this))
    , m_headerH(new QCheckBox(tr("First row is a header"), this))
    , m_headerV(new QCheckBox(tr("First column is a header"), this))
    , m_preview(new QChartView(this))
{
    setWindowTitle(tr("Insert chart"));
    m_kind->addItems({tr("Bar"), tr("Stacked bar"), tr("Line"), tr("Area"), tr("Pie"),
                      tr("Scatter")});
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->addRow(tr("Kind"), m_kind);
    form->addRow(tr("Title"), m_title);
    form->addRow(tr("Data range"), m_range);
    form->addRow(m_headerH);
    form->addRow(m_headerV);
    layout->addLayout(form);
    m_preview->setMinimumSize(480, 300);
    layout->addWidget(m_preview, 1);
    auto *buttons = new QHBoxLayout;
    auto *insert = new QPushButton(tr("Insert"), this);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    buttons->addStretch(1);
    buttons->addWidget(insert);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    connect(insert, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { rebuildPreview(); });
    connect(m_range, &QLineEdit::textChanged, this, [this] { rebuildPreview(); });
    rebuildPreview();
}

CalcEng::ChartSpec ChartDialog::spec() const
{
    CalcEng::ChartSpec out;
    out.kind = CalcEng::ChartSpec::Kind(qBound(0, m_kind->currentIndex(), 5));
    out.title = m_title->text();
    out.rangeText = m_range->text();
    out.headerH = m_headerH->isChecked();
    out.headerV = m_headerV->isChecked();
    return out;
}

void ChartDialog::rebuildPreview()
{
    if (m_preview->chart() != nullptr) {
        m_preview->chart()->deleteLater();
    }
    m_preview->setChart(buildChart(spec(), *m_workbook, m_preview));
}

} // namespace QindaOffice::Apps::QindaCalc

#endif // QQO_WITH_CHARTS
