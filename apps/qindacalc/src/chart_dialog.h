// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Chart creation UI for QindaCalc (plan section 7.2, QQO_WITH_CHARTS):
// pick a kind, title and data range, preview live with Qt Charts, then
// insert as a floating object anchored on the sheet. Everything compiles
// only when the suite is built with -DQQO_WITH_CHARTS=ON.

#ifdef QQO_WITH_CHARTS

#include "chart_spec.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QCheckBox;

namespace QindaOffice::Calc {
class Workbook;
}
namespace CalcEng = QindaOffice::Calc;

class QChartView;

namespace QindaOffice::Apps::QindaCalc {

// Builds a Qt Charts view for a spec against the live workbook (shared by
// the dialog preview, the floating objects and the tests).
[[nodiscard]] QWidget *chartWidgetFor(const CalcEng::ChartSpec &spec, const CalcEng::Workbook &wb,
                                      QWidget *parent = nullptr);

class ChartDialog final : public QDialog {
    Q_OBJECT
public:
    ChartDialog(CalcEng::Workbook *wb, const QString &initialRange, QWidget *parent = nullptr);

    [[nodiscard]] CalcEng::ChartSpec spec() const;

private:
    void rebuildPreview();

    CalcEng::Workbook *m_workbook;
    QComboBox *m_kind;
    QLineEdit *m_title;
    QLineEdit *m_range;
    QCheckBox *m_headerH;
    QCheckBox *m_headerV;
    QChartView *m_preview;
};

} // namespace QindaOffice::Apps::QindaCalc

#endif // QQO_WITH_CHARTS
