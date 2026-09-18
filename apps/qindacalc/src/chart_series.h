// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The data a chart draws, read from the workbook for a ChartSpec (the
// first row and/or column of the range name the series and categories,
// the numeric body fills the values). Widgets-free; the QML chart
// (ChartView from QtCharts, when QQO_WITH_CHARTS) builds its series from
// the map: {title, kind:int, categories:[string], series:[{name, values:[number]}]}.

#include "chart_spec.h"
#include "workbook.h"

#include <QVariantMap>

namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QVariantMap chartSeriesData(const QindaOffice::Calc::ChartSpec &spec,
                                          const QindaOffice::Calc::Workbook &workbook);

} // namespace QindaOffice::Apps::QindaCalc
