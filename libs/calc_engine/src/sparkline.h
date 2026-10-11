// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sparklines (M90, D-098): Excel 2010's tiny charts in a cell — line,
// column and win/loss — in Excel's model, so a worksheet's
// x14:sparklineGroups (calc_io's xlsx_sparklines.cpp) maps onto it one to
// one. A group holds the look (type, the points shown, colours, the axis)
// and its sparklines, each a cell it is drawn in and the row or column of
// data it shows (on any sheet). Groups live on their sheet
// (Sheet::sparklineGroups); structural edits move their cells and their
// data references (sparkline.cpp), and the values a sparkline draws come
// from sparklineSeries (sparkline_values.cpp) — screen and paper share it.
// Qt Core only.

#include "value.h"

#include <QList>
#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace QindaOffice::Calc {

class Sheet;
class Workbook;
struct StructureEdit;

// A colour as Excel writes it (CT_Color): an explicit RGB, or a theme
// colour (theme index, Excel's order: 0 Light 1, 1 Dark 1, 2 Light 2,
// 3 Dark 2, 4–9 Accent 1–6, 10 Hyperlink, 11 Followed Hyperlink) with a
// tint (−1 darker … +1 lighter). Unset: the element is absent.
struct SparklineColor {
    std::optional<Rgb> rgb;
    int theme = -1;
    double tint = 0;
    [[nodiscard]] bool isSet() const { return rgb.has_value() || theme >= 0; }
    bool operator==(const SparklineColor &) const = default;
};

struct Sparkline {
    CellAddr location;   // the cell it is drawn in (xm:sqref)
    QString data;        // the data, engine spelling, sheet-qualified ("Sheet1!B2:G2"); empty: none
    bool operator==(const Sparkline &) const = default;
};

struct SparklineGroup {
    enum class Type : quint8 { Line, Column, WinLoss };         // line, column, stacked
    enum class Empty : quint8 { Gap, Zero, Span };              // displayEmptyCellsAs
    enum class AxisScale : quint8 { Individual, Group, Custom }; // min/maxAxisType
    Type type = Type::Line;
    double lineWeight = 0.75; // points
    bool markers = false;
    bool high = false;
    bool low = false;
    bool first = false;
    bool last = false;
    bool negative = false;
    bool displayXAxis = false;
    bool displayHidden = false;
    bool rightToLeft = false;
    Empty empty = Empty::Gap;
    AxisScale minAxis = AxisScale::Individual;
    AxisScale maxAxis = AxisScale::Individual;
    double manualMin = 0;
    double manualMax = 0;
    bool dateAxis = false;
    QString dateRange; // the dates, sheet-qualified, when dateAxis
    SparklineColor series;
    SparklineColor negativeColor;
    SparklineColor axisColor;
    SparklineColor markersColor;
    SparklineColor firstColor;
    SparklineColor lastColor;
    SparklineColor highColor;
    SparklineColor lowColor;
    QList<Sparkline> sparklines;
    // The group's attributes the model does not name, as read (written back).
    QList<std::pair<QString, QString>> extraAttributes;
    bool operator==(const SparklineGroup &) const = default;
};

// Excel's new group of `type`: the colours of Excel's Insert ▸ Sparklines
// (Accent 1 darker 50 % for lines, darker 25 % for columns, Accent 2 for
// negative points, black axis), gaps for empty cells.
[[nodiscard]] SparklineGroup defaultSparklineGroup(SparklineGroup::Type type);

// Where a sparkline sits on a sheet: the group and the sparkline in it.
struct SparklineRef {
    qint32 group = -1;
    qint32 index = -1;
    [[nodiscard]] bool isValid() const { return group >= 0 && index >= 0; }
    bool operator==(const SparklineRef &) const = default;
};
[[nodiscard]] SparklineRef sparklineAt(const Sheet &sheet, const CellAddr &addr);

// Insert ▸ Sparklines: one sparkline per location cell over `data` (the
// location a row or a column of cells; the data a row or column per
// location cell — Excel's rule). nullopt, with Excel's message in `error`,
// when the ranges do not fit.
[[nodiscard]] std::optional<QList<Sparkline>> sparklinesFor(const Workbook &book, qint32 sheet, const QString &data,
                                                           const QString &location, QString *error);

// The values a sparkline draws (sparkline_values.cpp): one entry per data
// cell in order (right to left reversed by the painter), nullopt for a gap.
// Hidden rows and columns are left out unless the group shows them; empty
// cells are gaps, zeros or left out (Span) as the group says; text and
// errors count as empty. `dates` (date axis) holds each value's date.
struct SparklineSeries {
    QVector<std::optional<double>> values;
    QVector<double> dates; // empty unless the group has a date axis
};
[[nodiscard]] SparklineSeries sparklineSeries(const Workbook &book, const SparklineGroup &group, const Sparkline &line);

// The vertical axis of one sparkline: Automatic for Each Sparkline, Same
// for All Sparklines (the group's extremes) or Custom Value.
struct SparklineScale {
    double min = 0;
    double max = 0;
};
[[nodiscard]] SparklineScale sparklineScale(const Workbook &book, const SparklineGroup &group,
                                            const SparklineSeries &series);

// Structural edits and sheet renames (sparkline.cpp): the edited sheet's
// sparklines move with their cells (one whose cell goes, goes); every
// sheet's data references follow the edit like formulas, and a data range
// that is removed whole leaves the sparkline with no data.
void moveSparklinesForEdit(Workbook &book, const StructureEdit &edit);
void requalifySparklines(Workbook &book, const QString &from, const QString &to);

} // namespace QindaOffice::Calc
