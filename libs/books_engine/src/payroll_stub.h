// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The lines of a pay stub (M71, D-081): what QindaBooks prints on a
// paycheck's voucher stubs (books_io pay_stub.cpp) and shows in the app's
// paycheck dialog — built here once so paper and screen cannot drift.
//
// Gross pay, the federal taxes every check has and net pay always; for a
// business that chose Colorado its income tax and FAMLI too; a deduction,
// Additional Medicare, a city tax or one of the company's own payroll tax
// items only when this check or the year so far has one. Each line has
// this check's amount and the year to date through it. The employer's
// taxes (not on the printed stub) come last.

#include "money.h"

#include <QList>
#include <QString>

namespace QindaOffice::Books {

class CompanyFile;
struct PaycheckRecord;

struct StubLine {
    enum class Section { Earnings, Taxes, AfterTax, Net, Employer };
    Section section = Section::Earnings;
    QString label;
    Money current;
    Money yearToDate;
};

[[nodiscard]] QList<StubLine> payStubLines(CompanyFile &file, const PaycheckRecord &paycheck);

} // namespace QindaOffice::Books
