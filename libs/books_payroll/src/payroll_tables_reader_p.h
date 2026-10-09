// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private: reads one tax year's data file into PayrollTables
// (payroll_tables.h). Every value is required; the first missing or
// malformed one stops the read with its data-file path in the error, so a
// broken table never computes a paycheck.

#include "payroll_tables.h"

#include <QJsonObject>
#include <QJsonValue>

namespace QindaOffice::Books::Payroll {

class TablesReader {
public:
    explicit TablesReader(const QJsonObject &root)
        : m_root(root)
    {
    }
    bool read(PayrollTables &out);
    [[nodiscard]] QString error() const { return m_error; }

private:
    [[nodiscard]] QJsonValue value(const QString &path);
    Money money(const QString &path);
    Decimal rate(const QString &path);
    int integer(const QString &path);
    QString text(const QString &path, bool required = true);
    QDate date(const QString &path);
    QList<QDate> dates(const QString &path);
    RateSchedule schedule(const QString &path);
    QMap<FilingStatus, Money> byFilingStatus(const QString &path);
    QMap<FilingStatus, RateSchedule> schedulesByFilingStatus(const QString &path);
    QMap<W4Status, RateSchedule> schedulesByW4Status(const QString &path);
    QMap<PayFrequency, int> payPeriods(const QString &path);
    UiRate uiRate(const QString &path);
    bool fail(const QString &path, const QString &why);

    void readFederal(PayrollTables &out);
    void readEstimated(PayrollTables &out);
    void readColorado(PayrollTables &out);

    QJsonObject m_root;
    QString m_error;
};

} // namespace QindaOffice::Books::Payroll
