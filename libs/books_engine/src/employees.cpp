// SPDX-License-Identifier: GPL-3.0-or-later
// Employees (employees.h): the minimal list time is entered for.
#include "employees.h"

#include "company_file.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

const QString kColumns = QStringLiteral("id, name, email, phone, address, notes, default_rate, active");

Employee readEmployee(const QSqlQuery &q, const QString &currency)
{
    Employee e;
    e.id = q.value(0).toLongLong();
    e.name = q.value(1).toString();
    e.email = q.value(2).toString();
    e.phone = q.value(3).toString();
    e.address = q.value(4).toString();
    e.notes = q.value(5).toString();
    if (!q.value(6).isNull()) {
        e.defaultRate = Money(q.value(6).toLongLong(), currency);
    }
    e.active = q.value(7).toBool();
    return e;
}

QString employeeJson(const Employee &e)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("name"), e.name},
                                                       {QStringLiteral("email"), e.email},
                                                       {QStringLiteral("rate"), e.defaultRate ? e.defaultRate->toPlainString()
                                                                                              : QString()},
                                                       {QStringLiteral("active"), e.active}})
                                 .toJson(QJsonDocument::Compact));
}

QVariantList values(const Employee &e)
{
    return {e.name.trimmed(), e.email.trimmed(), e.phone.trimmed(), e.address, e.notes,
            e.defaultRate ? QVariant(e.defaultRate->minor()) : QVariant(), e.active ? 1 : 0, CompanyFile::nowStamp()};
}

} // namespace

Employees::Employees(CompanyFile &file)
    : m_file(file)
{
}

Result Employees::validate(const Employee &employee) const
{
    if (employee.name.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an employee needs a name"));
    }
    const EmployeeId other = find(employee.name);
    if (other != 0 && other != employee.id) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("%1 is already an employee").arg(employee.name.trimmed()));
    }
    if (employee.defaultRate) {
        if (employee.defaultRate->isNegative()) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("an hourly rate is not negative"));
        }
        if (!employee.defaultRate->sameCurrency(m_file.money(0))) {
            return Result::failure(ErrorCode::CurrencyMismatch, QStringLiteral("the rate must be in %1").arg(m_file.currency()));
        }
    }
    return Result::success();
}

Result Employees::create(const Employee &employee)
{
    Employee e = employee;
    e.id = 0;
    if (const Result valid = validate(e); !valid.ok) {
        return valid;
    }
    SqlTransaction txn(m_file);
    QString error;
    QVariantList binds = values(e);
    binds << CompanyFile::nowStamp();
    const EmployeeId id = Sql::insert(m_file.database(),
                                      QStringLiteral("INSERT INTO employees(name, email, phone, address, notes, default_rate,"
                                                     " active, updated_at, created_at) VALUES(?,?,?,?,?,?,?,?,?)"),
                                      binds, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("employee"), id, {}, employeeJson(e));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result Employees::update(const Employee &employee)
{
    const std::optional<Employee> before = this->employee(employee.id);
    if (!before) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such employee"));
    }
    if (const Result valid = validate(employee); !valid.ok) {
        return valid;
    }
    SqlTransaction txn(m_file);
    QString error;
    QVariantList binds = values(employee);
    binds << employee.id;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE employees SET name = ?, email = ?, phone = ?, address = ?, notes = ?,"
                                  " default_rate = ?, active = ?, updated_at = ? WHERE id = ?"),
                   binds, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("edit"), QStringLiteral("employee"), employee.id, employeeJson(*before),
                 employeeJson(employee));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(employee.id);
}

Result Employees::setActive(EmployeeId id, bool active)
{
    std::optional<Employee> e = employee(id);
    if (!e) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such employee"));
    }
    e->active = active;
    return update(*e);
}

std::optional<Employee> Employees::employee(EmployeeId id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM employees WHERE id = ?"), {id});
    if (!q.next()) {
        return std::nullopt;
    }
    return readEmployee(q, m_file.currency());
}

QList<Employee> Employees::employees(bool includeInactive) const
{
    QList<Employee> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM employees")
                    + (includeInactive ? QString() : QStringLiteral(" WHERE active = 1"))
                    + QStringLiteral(" ORDER BY name COLLATE NOCASE, id"));
    while (q.next()) {
        out << readEmployee(q, m_file.currency());
    }
    return out;
}

EmployeeId Employees::find(const QString &name) const
{
    return Sql::scalarInt(m_file.database(), QStringLiteral("SELECT id FROM employees WHERE name = ? COLLATE NOCASE"),
                          {name.trimmed()});
}

} // namespace QindaOffice::Books
