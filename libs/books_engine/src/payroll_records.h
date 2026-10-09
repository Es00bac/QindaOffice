// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll records in the company file (M71, D-081; docs/books.md
// "Payroll"): the company's payroll setup — how the IRS taxes the LLC, its
// tax account numbers, deposit schedule, UI rate, FAMLI shares, city — and
// each employee's payroll half (identity, address, Form W-4, DR 0004, pay,
// deductions). The arithmetic lives in books_payroll; this module only
// stores and validates.
//
// AGENT-CONTRACT (with M68's employees.h): payroll EXTENDS the `employees`
// list in its own table `employee_payroll` (schema v5) keyed by the
// employee id. Pay rates, tax setup and identity numbers live there, never
// in `employees`.
//
// AGENT-GUARD (identity numbers): an SSN or EIN typed by the user is stored
// only here, in the company file. Everywhere else it is masked to its last
// four digits (maskedSsn / maskedEin) — never in a log, an audit row's JSON,
// a status message or test output. The audit trail records that an SSN was
// set or changed, not its value.

#include "books_types.h"
#include "decimal.h"
#include "money.h"

#include "colorado_payroll.h"
#include "federal_withholding.h"
#include "payroll_calendar.h"
#include "payroll_types.h"

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

// How the IRS taxes the LLC (Form 8832 / Form 2553): it decides owner pay.
enum class EntityTax { Unsure = 0, ScheduleC = 1, SCorporation = 2, Partnership = 3 };
[[nodiscard]] QString entityTaxKey(EntityTax tax);
[[nodiscard]] std::optional<EntityTax> entityTaxFromKey(const QString &key);

// The accounts payroll posts to, by role (payrollAccountRoles()).
namespace PayrollRole {
inline const QString Wages = QStringLiteral("wages");
inline const QString OfficerWages = QStringLiteral("officerWages");
inline const QString EmployerTaxes = QStringLiteral("employerTaxes");
inline const QString FederalWithholding = QStringLiteral("federalWithholding");
inline const QString SocialSecurity = QStringLiteral("socialSecurity");
inline const QString Medicare = QStringLiteral("medicare");
inline const QString Futa = QStringLiteral("futa");
inline const QString ColoradoWithholding = QStringLiteral("coloradoWithholding");
inline const QString ColoradoUi = QStringLiteral("coloradoUi");
inline const QString Famli = QStringLiteral("famli");
inline const QString LocalOpt = QStringLiteral("localOpt");
inline const QString Section125 = QStringLiteral("section125");
inline const QString Retirement = QStringLiteral("retirement");
inline const QString AfterTax = QStringLiteral("afterTax");
inline const QString OwnerDraws = QStringLiteral("ownerDraws");
inline const QString Distributions = QStringLiteral("distributions");
} // namespace PayrollRole

struct PayrollAccountSpec {
    QString role;
    QString name;    // what QindaBooks names it when it creates it
    int type = 0;    // AccountType value
    QString parent;  // the role or chart name it sits under ("" = top level)
};
// Every role, in chart order: the expense accounts, the liability
// sub-accounts under "Payroll Liabilities", the two equity accounts.
[[nodiscard]] QList<PayrollAccountSpec> payrollAccountRoles();

struct PayrollSetup {
    EntityTax entity = EntityTax::Unsure;
    bool configured = false;           // the setup was saved once
    QString ein;                       // nine digits, stored plainly here only
    QString coloradoAccount;           // Colorado withholding account number (CAN)
    QString uiAccount;                 // CDLE employer account number
    QString famliAccount;              // My FAMLI+ employer account
    Payroll::DepositSchedule depositSchedule = Payroll::DepositSchedule::Monthly;
    Payroll::ColoradoFrequency coloradoFrequency = Payroll::ColoradoFrequency::Quarterly;
    // The UI rate CDLE assigned, part by part; empty = the year's
    // introductory rate (heavy construction when `uiHeavyConstruction`).
    Payroll::UiRate uiRate;
    bool uiHeavyConstruction = false;
    bool smallEmployer = true;         // fewer than 10 employees (FAMLI)
    Decimal famliEmployeeShare = Decimal(5, 1);
    Decimal famliEmployerShare = Decimal(0, 0); // a small employer need not pay its half
    QString optCity;                   // the company's city ("" = none)
    QString optFiling = QStringLiteral("quarterly"); // "monthly" | "quarterly" | "annual"
    bool roundFederalWithholding = false;
    qint64 payrollBankAccountId = 0;
    qint64 setAsideAccountId = 0;      // where owner-pay set-asides go
    QMap<QString, qint64> accounts;    // role → account id (ensurePayrollAccounts)
    QString estimateProfile;           // the estimated-tax planner's inputs (JSON)
    // The W-2 file (EFW2) submitter: the SSA Business Services Online user
    // ID, a contact, and the optional employer signature PIN (S-XXXXXX-Y).
    QString bsoUserId;
    QString w2ContactName;
    QString w2ContactPhone;
    QString w2ContactEmail;
    QString signaturePin;

    [[nodiscard]] qint64 account(const QString &role) const { return accounts.value(role, 0); }
};

// Deductions on every paycheck of an employee (per pay period).
struct PayDeductions {
    Money section125;                  // pre-tax health premiums (cafeteria plan)
    enum class Retirement { None = 0, Plan401k = 1, SimpleIra = 2 };
    Retirement retirement = Retirement::None;
    Money retirementAmount;            // a fixed amount per period, or …
    Decimal retirementPercent;         // … a share of gross pay (when the amount is zero)
    Money shareholderHealth;           // 2 % shareholder's health premiums the company pays
    Money afterTax;                    // after-tax deduction per period
    QString afterTaxLabel;
};

struct EmployeePayroll {
    qint64 employeeId = 0;
    QString firstName;
    QString middleInitial;
    QString lastName;
    QString suffix;
    QString ssn;                       // nine digits; masked everywhere but the editor and the W-2
    QString street;
    QString city;
    QString state = QStringLiteral("CO");
    QString zip;
    QDate hireDate;
    QDate releaseDate;
    enum class PayType { Salary = 0, Hourly = 1 };
    PayType payType = PayType::Salary;
    Decimal payRate;                   // salary: dollars a year; hourly: dollars an hour
    Payroll::PayFrequency frequency = Payroll::PayFrequency::Biweekly;
    Decimal defaultHours;              // hourly: the usual hours per pay period
    Payroll::W4Certificate w4;
    bool w4OnFile = true;
    Payroll::ColoradoCertificate dr0004;
    bool coloradoWithholding = true;   // Colorado resident or works in Colorado
    QString optCity;                   // "" = the company's city; "none" = none
    bool ownerEmployee = false;        // an owner on the payroll (officer wages)
    bool shareholder2Percent = false;  // > 2 % S-corp shareholder (health premiums in box 1 only)
    PayDeductions deductions;
    bool exists = false;               // a row is stored

    [[nodiscard]] QString fullName() const;
};

// "•••-••-6789"; "" without an SSN.
[[nodiscard]] QString maskedSsn(const QString &ssn);
// "••-•••6789"; "" without an EIN.
[[nodiscard]] QString maskedEin(const QString &ein);
// The digits of an identity number, or "" when it is not `digits` long.
[[nodiscard]] QString normalizedTaxId(const QString &text, int digits);

class PayrollRecords {
public:
    explicit PayrollRecords(CompanyFile &file);

    [[nodiscard]] PayrollSetup setup() const;
    // Validates (EIN nine digits when given, shares within the FAMLI
    // limits, a known city) and stores; audited without identity numbers.
    Result saveSetup(const PayrollSetup &setup);
    // Creates every payroll account the setup lacks (finding an existing
    // account of the right type by name first — "Payroll Liabilities",
    // "Owner Draws") and stores the ids. Idempotent.
    Result ensurePayrollAccounts();

    [[nodiscard]] EmployeePayroll employee(qint64 employeeId) const; // defaults when none stored
    [[nodiscard]] QList<EmployeePayroll> employees(bool includeReleased = false) const;
    // Validates (SSN nine digits when given, a pay rate, W-4 numbers not
    // negative, the employee exists) and stores; audited without the SSN.
    Result saveEmployee(const EmployeePayroll &record);

    // The employee's gross pay for one period: the annual salary ÷ the
    // periods in a year, or the hourly rate × hours (half-up to the cent).
    [[nodiscard]] Money grossPay(const EmployeePayroll &record, const std::optional<Decimal> &hours = {}) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
