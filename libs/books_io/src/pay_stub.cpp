// SPDX-License-Identifier: GPL-3.0-or-later
// A paycheck's pay stub on voucher check stock (pay_stub_p.h).
#include "pay_stub_p.h"

#include "company_file.h"
#include "documents.h"
#include "paint_util.h"
#include "payroll_ledger.h"
#include "payroll_records.h"
#include "printable_document.h"

#include <QFontMetricsF>
#include <QPainter>

namespace QindaOffice::Books {

using namespace Paint;

namespace {

QString money(const Money &m, const QLocale &locale)
{
    return m.format(locale, false);
}

QString shortDate(const QDate &d, const QLocale &locale)
{
    return locale.toString(d, QStringLiteral("MMM d, yyyy"));
}

} // namespace

bool fillPayStub(CompanyFile &file, const BusinessDocument &doc, PrintableCheck *c, QString *error)
{
    const std::optional<PaycheckRecord> r = PayrollLedger(file).paycheck(doc.id);
    if (!r) {
        if (error != nullptr) {
            *error = QStringLiteral("this paycheck has no payroll record");
        }
        return false;
    }
    const EmployeePayroll e = PayrollRecords(file).employee(r->employeeId);
    const QLocale locale = companyLocale(file);
    c->payee = e.fullName();
    c->address = QStringList{c->payee};
    if (!e.street.trimmed().isEmpty()) {
        c->address << e.street.trimmed();
    }
    const QString cityLine = QStringLiteral("%1, %2 %3").arg(e.city.trimmed(), e.state.trimmed(), e.zip.trimmed()).trimmed();
    if (!e.city.trimmed().isEmpty()) {
        c->address << cityLine;
    }
    PrintablePayStub s;
    s.employee = c->payee;
    s.maskedSsn = maskedSsn(e.ssn);
    s.payDate = shortDate(r->payDate, locale);
    if (r->periodStart.isValid() && r->periodEnd.isValid()) {
        s.period = shortDate(r->periodStart, locale) + QStringLiteral(" – ") + shortDate(r->periodEnd, locale);
    }
    const Money dollar = file.money(100);
    if (e.payType == EmployeePayroll::PayType::Hourly) {
        s.rate = QStringLiteral("%1 h at %2").arg(r->hours, dollar.multiplied(e.payRate).format(locale));
    } else {
        s.rate = QStringLiteral("%1 a year").arg(dollar.multiplied(e.payRate).format(locale));
    }
    if (c->memo.isEmpty()) {
        c->memo = s.period.isEmpty() ? QStringLiteral("Payroll") : QStringLiteral("Pay period ") + s.period;
    }
    // The year to date through this check: the paychecks before it, plus it.
    const PayrollYearToDate y = PayrollLedger(file).yearToDate(r->employeeId, r->payDate, r->docId);
    const Payroll::PaycheckTaxes &t = r->taxes;
    const auto line = [&](const QString &label, const Money &now, const Money &before) {
        return PayStubLine{label, money(now, locale), money(now + before, locale)};
    };
    s.earnings << line(QStringLiteral("Gross pay"), t.gross, y.gross);
    if (!(r->section125 + y.section125).isZero()) {
        s.earnings << line(QStringLiteral("Health insurance (before tax)"), r->section125, y.section125);
    }
    if (!(r->retirement + y.retirement).isZero()) {
        s.earnings << line(QStringLiteral("Retirement plan (before tax)"), r->retirement, y.retirement);
    }
    s.taxes << line(QStringLiteral("Federal income tax"), t.federalIncomeTax, y.federalIncomeTax)
            << line(QStringLiteral("Social security"), t.socialSecurity, y.socialSecurity)
            << line(QStringLiteral("Medicare"), t.medicare, y.medicare);
    if (!(t.additionalMedicare + y.additionalMedicare).isZero()) {
        s.taxes << line(QStringLiteral("Additional Medicare"), t.additionalMedicare, y.additionalMedicare);
    }
    s.taxes << line(QStringLiteral("Colorado income tax"), t.coloradoIncomeTax, y.coloradoIncomeTax)
            << line(QStringLiteral("Colorado FAMLI"), t.famliEmployee, y.famliEmployee);
    if (!(t.optEmployee + y.optEmployee).isZero()) {
        s.taxes << line(QStringLiteral("Local OPT"), t.optEmployee, y.optEmployee);
    }
    if (!(r->afterTax + y.afterTax).isZero()) {
        const QString label = e.deductions.afterTaxLabel.isEmpty() ? QStringLiteral("Deduction after tax")
                                                                   : e.deductions.afterTaxLabel;
        s.after << line(label, r->afterTax, y.afterTax);
    }
    s.net = line(QStringLiteral("Net pay"), t.netPay, y.net);
    if (r->shareholderHealth.isPositive()) {
        s.notes << QStringLiteral("Health insurance paid for you: %1 (W-2 box 1)").arg(r->shareholderHealth.format(locale));
    }
    c->payStub = s;
    return true;
}

void drawPayStub(QPainter *p, const PrintableCheck &c, qreal top, const StubGeometry &g)
{
    const PrintablePayStub &s = *c.payStub;
    const QFont heading = font(10, QFont::DemiBold);
    QFont label = font(7, QFont::DemiBold);
    label.setLetterSpacing(QFont::PercentageSpacing, 108);
    const QFont body = font(8);
    const QFont small = font(7);
    const qreal width = g.right - g.left;
    qreal y = top + g.pad;
    QString facts = s.payDate;
    if (!c.number.isEmpty()) {
        facts += QStringLiteral("    No. ") + c.number;
    }
    drawText(p, g.right, y, facts, heading, kInk, true);
    drawText(p, g.left, y, elide(heading, s.employee, width - textWidth(heading, facts) - 24), heading);
    y += lineHeight(heading) + 1;
    QStringList sub;
    if (!s.period.isEmpty()) {
        sub << QStringLiteral("Pay period ") + s.period;
    }
    sub << s.rate;
    if (!s.maskedSsn.isEmpty()) {
        sub << QStringLiteral("SSN ") + s.maskedSsn;
    }
    drawText(p, g.left, y, elide(small, sub.join(QStringLiteral("  ·  ")), width), small, kMuted);
    y += lineHeight(small) + 6;
    // Two tables side by side: what was earned and taken before tax (and
    // after), and the taxes withheld — each with this check and the year.
    const qreal gap = 18;
    const qreal half = (width - gap) / 2;
    const auto table = [&](qreal x, const QString &title, const QList<PayStubLine> &rows) {
        const qreal yearX = x + half;
        const qreal nowX = x + half - 70;
        qreal ty = y;
        drawText(p, x, ty, title.toUpper(), label, kMuted);
        drawText(p, nowX, ty, QStringLiteral("THIS CHECK"), label, kMuted, true);
        drawText(p, yearX, ty, QStringLiteral("YEAR TO DATE"), label, kMuted, true);
        ty += lineHeight(label) + 2;
        p->setPen(QPen(kRule, 0.75));
        p->drawLine(QPointF(x, ty), QPointF(x + half, ty));
        ty += 3;
        for (const PayStubLine &row : rows) {
            drawText(p, x, ty, elide(body, row.label, half - 150), body);
            drawText(p, nowX, ty, row.current, body, kInk, true);
            drawText(p, yearX, ty, row.yearToDate, body, kInk, true);
            ty += lineHeight(body) + 1.5;
        }
        return ty;
    };
    QList<PayStubLine> left = s.earnings;
    left << s.after;
    const qreal leftEnd = table(g.left, QStringLiteral("Earnings and deductions"), left);
    const qreal rightEnd = table(g.left + half + gap, QStringLiteral("Taxes withheld"), s.taxes);
    qreal noteY = qMax(leftEnd, rightEnd) + 2;
    for (const QString &note : s.notes) {
        drawText(p, g.left, noteY, elide(small, note, width), small, kMuted);
        noteY += lineHeight(small);
    }
    // Net pay, this check and the year, at the stub's foot.
    const qreal footerTop = top + g.height - g.pad - lineHeight(heading);
    p->setPen(QPen(kRule, 0.75));
    p->drawLine(QPointF(g.left, footerTop - 5), QPointF(g.right, footerTop - 5));
    const QString net = QStringLiteral("Net pay   %1      Year to date   %2").arg(s.net.current, s.net.yearToDate);
    drawText(p, g.right, footerTop, net, heading, kInk, true);
    const QString drawn = c.companyName + QStringLiteral("  ·  ") + c.bankAccount;
    drawText(p, g.left, footerTop + 1, elide(small, drawn, width - textWidth(heading, net) - 24), small, kMuted);
}

} // namespace QindaOffice::Books
