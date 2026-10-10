// SPDX-License-Identifier: GPL-3.0-or-later
// The task sheet → xlsx through calc_io (plan_xlsx.h).
#include "plan_xlsx.h"

#include "baselines.h"
#include "custom_fields.h"
#include "plan_edit.h"
#include "plan_time.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <QHash>

#include <array>

namespace QindaOffice::Plan {

namespace {

namespace Calc = ::QindaOffice::Calc;

enum class Cell { Text, Money, Date, Days, Hours, Percent, Whole };

Cell kindOf(const QString &field)
{
    static const QHash<QString, Cell> kinds{
        {QStringLiteral("cost"), Cell::Money},           {QStringLiteral("fixedCost"), Cell::Money},
        {QStringLiteral("actualCost"), Cell::Money},     {QStringLiteral("remainingCost"), Cell::Money},
        {QStringLiteral("baselineCost"), Cell::Money},   {QStringLiteral("costVariance"), Cell::Money},
        {QStringLiteral("start"), Cell::Date},           {QStringLiteral("finish"), Cell::Date},
        {QStringLiteral("actualStart"), Cell::Date},     {QStringLiteral("actualFinish"), Cell::Date},
        {QStringLiteral("baselineStart"), Cell::Date},   {QStringLiteral("baselineFinish"), Cell::Date},
        {QStringLiteral("earlyStart"), Cell::Date},      {QStringLiteral("earlyFinish"), Cell::Date},
        {QStringLiteral("lateStart"), Cell::Date},       {QStringLiteral("lateFinish"), Cell::Date},
        {QStringLiteral("constraintDate"), Cell::Date},  {QStringLiteral("deadline"), Cell::Date},
        {QStringLiteral("duration"), Cell::Days},        {QStringLiteral("baselineDuration"), Cell::Days},
        {QStringLiteral("totalSlack"), Cell::Days},      {QStringLiteral("freeSlack"), Cell::Days},
        {QStringLiteral("startVariance"), Cell::Days},   {QStringLiteral("finishVariance"), Cell::Days},
        {QStringLiteral("durationVariance"), Cell::Days}, {QStringLiteral("work"), Cell::Hours},
        {QStringLiteral("percent"), Cell::Percent},      {QStringLiteral("id"), Cell::Whole},
        {QStringLiteral("priority"), Cell::Whole},
    };
    return kinds.value(field, Cell::Text);
}

QString symbolOf(const QString &currency)
{
    static const QHash<QString, QString> symbols{{QStringLiteral("USD"), QStringLiteral("$")},
                                                 {QStringLiteral("EUR"), QStringLiteral("€")},
                                                 {QStringLiteral("GBP"), QStringLiteral("£")},
                                                 {QStringLiteral("JPY"), QStringLiteral("¥")}};
    return symbols.value(currency, currency + QLatin1Char(' '));
}

// The minutes (or minor units) a numeric field holds; nullopt = no value.
std::optional<qint64> raw(const Project &p, const Task &t, const QString &field)
{
    const BaselineData &b = t.baselines[0];
    const Variance v = variance(p, t, 0);
    const qint64 actual = divRound(t.cost * t.percentComplete, 100);
    static const QHash<QString, int> index{
        {QStringLiteral("cost"), 0},          {QStringLiteral("fixedCost"), 1},     {QStringLiteral("actualCost"), 2},
        {QStringLiteral("remainingCost"), 3}, {QStringLiteral("baselineCost"), 4},  {QStringLiteral("costVariance"), 5},
        {QStringLiteral("start"), 6},         {QStringLiteral("finish"), 7},        {QStringLiteral("actualStart"), 8},
        {QStringLiteral("actualFinish"), 9},  {QStringLiteral("baselineStart"), 10}, {QStringLiteral("baselineFinish"), 11},
        {QStringLiteral("earlyStart"), 12},   {QStringLiteral("earlyFinish"), 13},  {QStringLiteral("lateStart"), 14},
        {QStringLiteral("lateFinish"), 15},   {QStringLiteral("constraintDate"), 16}, {QStringLiteral("deadline"), 17},
        {QStringLiteral("duration"), 18},     {QStringLiteral("baselineDuration"), 19}, {QStringLiteral("totalSlack"), 20},
        {QStringLiteral("freeSlack"), 21},    {QStringLiteral("startVariance"), 22}, {QStringLiteral("finishVariance"), 23},
        {QStringLiteral("durationVariance"), 24}, {QStringLiteral("work"), 25},     {QStringLiteral("percent"), 26},
        {QStringLiteral("id"), 27},           {QStringLiteral("priority"), 28}};
    const auto date = [](Minutes m) { return m == kNoDate ? std::nullopt : std::optional<qint64>(m); };
    switch (index.value(field, -1)) {
    case 0: return t.cost;
    case 1: return t.fixedCost;
    case 2: return actual;
    case 3: return t.cost - actual;
    case 4: return b.set ? std::optional<qint64>(b.cost) : std::nullopt;
    case 5: return v.valid ? std::optional<qint64>(v.cost) : std::nullopt;
    case 6: return date(t.start);
    case 7: return date(t.finish);
    case 8: return date(t.actualStart);
    case 9: return date(t.actualFinish);
    case 10: return b.set ? date(b.start) : std::nullopt;
    case 11: return b.set ? date(b.finish) : std::nullopt;
    case 12: return date(t.earlyStart);
    case 13: return date(t.earlyFinish);
    case 14: return date(t.lateStart);
    case 15: return date(t.lateFinish);
    case 16: return constraintNeedsDate(t.constraint) ? date(t.constraintDate) : std::nullopt;
    case 17: return date(t.deadline);
    case 18: return t.duration;
    case 19: return b.set ? std::optional<qint64>(b.duration) : std::nullopt;
    case 20: return t.totalSlack;
    case 21: return t.freeSlack;
    case 22: return v.valid ? std::optional<qint64>(v.start) : std::nullopt;
    case 23: return v.valid ? std::optional<qint64>(v.finish) : std::nullopt;
    case 24: return v.valid ? std::optional<qint64>(v.duration) : std::nullopt;
    case 25: return t.work;
    case 26: return t.percentComplete;
    case 27: return t.id;
    case 28: return t.priority;
    default: return std::nullopt;
    }
}

} // namespace

bool writeTaskSheetXlsx(const Project &project, const QStringList &fields, const QString &path, const QLocale &locale,
                        QString *error)
{
    Calc::Workbook book;
    Calc::Sheet &sheet = book.addSheet(QStringLiteral("Task Sheet"));
    const int decimals = currencyDecimals(project.info.currency);
    double unit = 1;
    for (int i = 0; i < decimals; ++i) {
        unit *= 10;
    }
    const QString symbol = QString(symbolOf(project.info.currency)).replace(QLatin1Char('"'), QStringLiteral("\"\""));
    const QString moneyBody = QStringLiteral("#,##0") + (decimals > 0 ? QStringLiteral(".") + QString(decimals, QLatin1Char('0')) : QString());
    const auto style = [&book](const QString &format, bool bold = false, int align = 0) {
        Calc::CellStyle st;
        st.numberFormat = format;
        st.bold = bold;
        st.alignment = align;
        st.borders = bold ? 1 << 3 : 0;
        return book.addStyle(st);
    };
    // Indexed by Cell.
    const std::array<qint32, 7> styles{
        style(QStringLiteral("General")),
        style(QStringLiteral("\"%1\"%2;-\"%1\"%2").arg(symbol, moneyBody), false, 2),
        style(QStringLiteral("yyyy-mm-dd hh:mm")),
        style(QStringLiteral("0.##\" d\""), false, 2),
        style(QStringLiteral("0.##\" h\""), false, 2),
        style(QStringLiteral("0%"), false, 2),
        style(QStringLiteral("0"), false, 2),
    };
    const auto styleOf = [&styles](Cell kind) { return styles[size_t(kind)]; };
    const qint32 header = style(QStringLiteral("General"), true);
    const qint32 headerRight = style(QStringLiteral("General"), true, 2);
    const double minutesPerDay = qMax(1, project.info.minutesPerDay);
    book.beginBulkLoad();
    const auto put = [&](qint32 row, qint32 col, const Calc::Value &value, qint32 styleId) {
        book.setCellValue(0, {row, col}, value);
        sheet.setStyleIdAt({row, col}, styleId);
    };
    for (qint32 c = 0; c < fields.size(); ++c) {
        const QString &f = fields.at(c);
        // M83: a custom field under its new name ("Owner", not "Text1").
        const QString title = isCustomField(f) ? customFieldTitle(project, f) : taskFieldTitle(f);
        put(0, c, Calc::Value(title), kindOf(f) == Cell::Text ? header : headerRight);
        sheet.setColumnWidth(c, f == QLatin1String("name") ? 280 : kindOf(f) == Cell::Date ? 140 : 110);
    }
    for (qint32 r = 0; r < project.tasks.size(); ++r) {
        const Task &t = project.tasks.at(r);
        for (qint32 c = 0; c < fields.size(); ++c) {
            const QString &f = fields.at(c);
            const Cell kind = kindOf(f);
            if (kind == Cell::Text) {
                QString text = taskFieldText(project, r, f, locale);
                if (f == QLatin1String("name")) {
                    text.prepend(QString(qMax(0, t.outlineLevel - 1) * 3, QLatin1Char(' ')));
                }
                if (!text.isEmpty()) {
                    put(r + 1, c, Calc::Value(text), styleOf(Cell::Text));
                }
                continue;
            }
            const std::optional<qint64> v = raw(project, t, f);
            if (!v) {
                continue;
            }
            double value = 0;
            switch (kind) {
            case Cell::Money: value = double(*v) / unit; break;
            // Wall-clock minutes since 1970 → Excel's serial days.
            case Cell::Date: value = double(QDate(1899, 12, 30).daysTo(dateOf(*v))) + minuteOfDay(*v) / 1440.0; break;
            case Cell::Days: value = double(*v) / minutesPerDay; break;
            case Cell::Hours: value = double(*v) / 60.0; break;
            case Cell::Percent: value = double(*v) / 100.0; break;
            case Cell::Whole:
            case Cell::Text: value = double(*v); break;
            }
            put(r + 1, c, Calc::Value(value), styleOf(kind));
        }
    }
    book.endBulkLoad();
    sheet.setFreeze(1, 0);
    QString failure;
    if (!Calc::IO::writeXlsxFile(book, path, &failure)) {
        if (error != nullptr) {
            *error = failure.isEmpty() ? QStringLiteral("The workbook could not be written.") : failure;
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Plan
