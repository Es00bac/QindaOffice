// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PayrollService: the QindaBooks window's owner pay, estimated taxes and
// payroll for QML (M71, D-081; docs/books.md "Payroll (app)"), exposed as
// `BooksHost.payroll`. It holds no engine state: every call asks the
// window's BooksService for the current company file (Save As replaces it,
// books_service.h AGENT-GUARD) and for "today".
//
// AGENT-CONTRACT (QML API, the views and dialogs in qml/Payroll*.qml call
// these exact names — never rename one; same conventions as BooksService):
//  * money crosses as integer minor units + formatted text; inputs are
//    typed text (company locale, then C locale) — never a double;
//  * dates are ISO "yyyy-MM-dd"; every mutating call returns {ok, id, error};
//  * changed() fires with BooksService::changed (every committed mutation).
//
// AGENT-GUARD (identity numbers): SSNs and EINs leave this service masked
// ("•••-••-6789") except employee(id).ssn and setup().ein — the edit
// fields — and the forms saved for filing (exportFormForFiling, exportEfw2).
// They never go to notify(), a toast or an error text.
//
// Files: payroll_service.cpp (setup, owners, employees),
// payroll_service_items.cpp (the company's own payroll tax items, and each
// employee's settings for them), payroll_service_pay.cpp (Run Payroll,
// paychecks, liabilities, filing exports), payroll_service_owner.cpp (owner
// pay, estimated taxes, due-date reminders).

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Books {
class CompanyFile;
struct EmployeePayroll;
struct PaycheckRequest;
}

namespace QindaOffice::Apps::Books {

class BooksService;

class PayrollService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The window's BooksHost owns its PayrollService.")
    // How the business is taxed: "unsure" | "scheduleC" (a sole
    // proprietorship or single-member LLC) | "partnership" | "sCorporation"
    // | "cCorporation" | "nonprofit".
    Q_PROPERTY(QString entity READ entity NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    // Colorado's built-in tables apply (the business chose Colorado).
    Q_PROPERTY(bool colorado READ colorado NOTIFY changed)
    // The tables' year for today ("" when the year has none).
    Q_PROPERTY(int tablesYear READ tablesYear NOTIFY changed)
public:
    explicit PayrollService(BooksService *books, QObject *parent = nullptr);

    [[nodiscard]] QString entity() const;
    [[nodiscard]] bool configured() const;
    [[nodiscard]] bool colorado() const;
    [[nodiscard]] int tablesYear() const;

    // ---- setup, owners, employees (payroll_service.cpp) ----
    // {entity, configured, ein, einMasked, colorado, stateCode,
    //  coloradoAccount, uiAccount, famliAccount, depositSchedule,
    //  coloradoFrequency, uiRateParts[{name, rate}], uiRateText,
    //  uiHeavyConstruction, smallEmployer, famliEmployeeShare,
    //  famliEmployerShare (percent text), optCity, optFiling,
    //  roundFederalWithholding, payrollBankAccountId, setAsideAccountId,
    //  bsoUserId, w2ContactName/Phone/Email, signaturePin}
    Q_INVOKABLE QVariantMap setup() const;
    // The same keys; `uiRate` as "1.53%, 0.17%, 1.35%" (empty = the
    // introductory rate). Creates the payroll accounts.
    Q_INVOKABLE QVariantMap saveSetup(const QVariantMap &map);
    // {year, ok, error, sources[{title, revised, url}], unconfirmed[{label,
    //  note}], cities[{key, city, employeeText, employerText, threshold,
    //  filing, status}], introductoryRateText, coloradoMonthlyFrom,
    //  coloradoWeeklyAbove, famliSmallEmployerSize}
    Q_INVOKABLE QVariantMap tablesInfo() const;
    Q_INVOKABLE QVariantList owners() const;                  // [{id, name, address}]
    Q_INVOKABLE QVariantMap saveOwner(const QVariantMap &map); // {id, name, address}
    // [{id, name, hasPayroll, payTypeText, payText, frequencyText,
    //   ssnMasked, ownerEmployee, released}] — every M68 employee.
    Q_INVOKABLE QVariantList employees() const;
    // The editor's map (the one place an SSN leaves in full), with
    // customItems[{id, name, kind, kindLabel, subject, percent, extra}] for
    // the company's active payroll tax items.
    Q_INVOKABLE QVariantMap employee(qint64 id) const;
    // id 0 creates the M68 employee first; returns {ok, id, error}.
    // customItems as employee() gives them (percent "4%" and extra as text).
    Q_INVOKABLE QVariantMap saveEmployee(const QVariantMap &map);

    // ---- the company's own payroll tax items (payroll_service_items.cpp) ----
    // [{id, name, side, sideLabel, kind, kindLabel, rate ("2.7%"), wageBase,
    //  wageBaseText ("$10,000.00"), annualLimit (text, "" = none),
    //  accountId, accountName, agency, frequency, frequencyLabel, dueDay,
    //  active}], inactive ones included.
    Q_INVOKABLE QVariantList payrollItems() const;
    // The same keys (id 0 = new; accountId 0 = create the account) →
    // {ok, id, error}.
    Q_INVOKABLE QVariantMap savePayrollItem(const QVariantMap &map);
    Q_INVOKABLE QVariantMap setPayrollItemActive(qint64 id, bool active);

    // ---- Run Payroll, paychecks, liabilities (payroll_service_pay.cpp) ----
    // {payDate, periodStart, periodEnd, bankAccountId, rows[{employeeId,
    //  name, hourly, hours, payText, bonus}]} for the active employees.
    Q_INVOKABLE QVariantMap payrollDraft(const QString &payDateIso) const;
    // The run without posting: {ok, error, rows[{employeeId, name, ok,
    //  error, gross(Text), fit(Text), ss, medicare, colorado, famli, opt,
    //  deductions, net(Text), employer(Text), notes[]}], totals{…Text}}.
    Q_INVOKABLE QVariantMap previewPayroll(const QVariantMap &run) const;
    // Posts every row's paycheck in one transaction: {ok, ids[], error}.
    Q_INVOKABLE QVariantMap runPayroll(const QVariantMap &run);
    Q_INVOKABLE QVariantList paychecks(const QString &fromIso, const QString &toIso) const;
    // One paycheck with its stub lines (this check, year to date), the
    // Pub 15-T and DR 1098 worksheets and notes.
    Q_INVOKABLE QVariantMap paycheck(qint64 id) const;
    Q_INVOKABLE QVariantMap voidPaycheck(qint64 id, const QString &reason);
    // Void + rewrite with {hours, pay, bonus, payDate} changes.
    Q_INVOKABLE QVariantMap adjustPaycheck(qint64 id, const QVariantMap &corrected);
    // [{key, tax, label, agency, period, start, end, due, dueText,
    //   accrued(Text), paid(Text), balance(Text), notes, overdue}]
    Q_INVOKABLE QVariantList liabilities(const QString &fromIso, const QString &toIso, bool openOnly) const;
    // {tax, start, end, bankAccountId, date, amount (text, "" = balance), reference}
    Q_INVOKABLE QVariantMap payLiability(const QVariantMap &map);
    Q_INVOKABLE QVariantList liabilityPayments(int year) const;
    Q_INVOKABLE QVariantMap voidLiabilityPayment(qint64 id, const QString &reason);
    // The W-2 file for SSA (EFW2): {ok, error, path, employees, warnings[]}.
    Q_INVOKABLE QVariantMap exportEfw2(int year, const QUrl &fileUrl);
    // A form worksheet as PDF WITH full identity numbers, for filing:
    // form_w2, form_w3, ui_wage_report, famli_wage_report.
    Q_INVOKABLE QVariantMap exportFormForFiling(const QString &id, const QString &toIso, const QUrl &fileUrl);

    // ---- owner pay, estimated taxes, reminders (payroll_service_owner.cpp) ----
    // {kind "draw" | "distribution" | "dividend", ownerId, bankAccountId,
    //  amount, date, method "check" | "transfer", checkNumber, toPrint,
    //  memo} → {ok, id, error, setAsidePercent, setAside, setAsideText}
    //  (the set-aside for a draw); refused when the business type does not
    //  pay its owners that way.
    Q_INVOKABLE QVariantMap payOwner(const QVariantMap &map);
    // {amount, date, bankAccountId, toAccountId}
    Q_INVOKABLE QVariantMap recordSetAside(const QVariantMap &map);
    // {jurisdiction "federal" | "colorado", taxYear, installment, amount,
    //  date, bankAccountId, reference}
    Q_INVOKABLE QVariantMap recordEstimatedPayment(const QVariantMap &map);
    // [{id, kind, kindLabel, date, ownerName, amount(Text), number, memo, voided}]
    Q_INVOKABLE QVariantList ownerPayments(int year) const;
    Q_INVOKABLE QVariantMap voidOwnerPayment(qint64 id, const QString &reason);
    // The planner's inputs (filingStatus, projectedProfit, otherIncome, …
    // as text) and saving them with the company.
    Q_INVOKABLE QVariantMap estimateProfile() const;
    Q_INVOKABLE QVariantMap saveEstimateProfile(const QVariantMap &map);
    // The plan for this year from the books' profit so far and the
    // profile: {ok, error, year, asOf, ytdProfitText, annualProfitText,
    //  annualized, seTaxText, incomeTaxText, coloradoTaxText, qbiText,
    //  seWorksheet[], federal{…}, colorado{…}, setAsidePercent,
    //  businessTaxText, notes[]}; each plan {totalTaxText, requiredText,
    //  basis, required, reason, paidText, remainingText, worksheet[],
    //  installments[{number, due, amountText, paidText, owedText, state,
    //  stateLabel}]}.
    Q_INVOKABLE QVariantMap estimate() const;
    // The set-aside suggested for a draw of `amountText`: {percent, amount, amountText}.
    Q_INVOKABLE QVariantMap setAsideFor(const QString &amountText) const;
    // Due within `days` (or overdue): estimated installments, payroll
    // liabilities, quarterly and annual returns — [{label, date, amountText, kind}].
    Q_INVOKABLE QVariantList upcoming(int days) const;
    // Posts each due item to BooksService::notify once per window session.
    void remindDueDates();

signals:
    void changed();

private:
    [[nodiscard]] ::QindaOffice::Books::CompanyFile *file() const;
    bool requestFrom(const QVariantMap &row, const QVariantMap &run, ::QindaOffice::Books::PaycheckRequest *request,
                     QString *error) const;
    // The employee map's customItems, and reading them back (payroll_service_items.cpp).
    [[nodiscard]] QVariantList customItemsFor(const ::QindaOffice::Books::EmployeePayroll &employee) const;
    bool applyCustomItems(const QVariantList &rows, ::QindaOffice::Books::EmployeePayroll *employee, QString *error) const;
    BooksService *m_books;
    QSet<QString> m_reminded;
};

} // namespace QindaOffice::Apps::Books
