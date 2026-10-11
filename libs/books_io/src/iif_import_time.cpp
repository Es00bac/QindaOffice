// SPDX-License-Identifier: GPL-3.0-or-later
// QuickBooks IIF employee and time rows. This remains inside the import's
// outer transaction, so preview, refusal and commit use identical work.
#include "iif_import_p.h"

#include "employees.h"
#include "time_tracking.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace QindaOffice::Books::IifInternal {

namespace {

QString lower(const QString &value) { return value.trimmed().toLower(); }

bool yes(const QString &value)
{
    const QString v = value.trimmed().toUpper();
    return v == QLatin1String("Y") || v == QLatin1String("YES")
        || v == QLatin1String("TRUE") || v == QLatin1String("1");
}

QString employeeName(const IifRecord &row)
{
    const QString name = row.value(QStringLiteral("NAME"));
    return name.isEmpty()
        ? QStringList{row.value(QStringLiteral("FIRSTNAME")), row.value(QStringLiteral("LASTNAME"))}
              .join(QLatin1Char(' ')).trimmed()
        : name;
}

QString address(const IifRecord &row, const QString &name)
{
    QStringList lines;
    for (int i = 1; i <= 5; ++i) {
        const QString line = row.value(QStringLiteral("ADDR%1").arg(i));
        if (lines.isEmpty() && i == 1 && line.compare(name, Qt::CaseInsensitive) == 0) continue;
        if (!line.isEmpty()) lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

// A counted signature skips precisely the existing occurrences of a row.
// Two identical rows in the first IIF file remain two activities, while
// importing that file a second time adds neither one.
QString signature(const TimeEntry &entry)
{
    const QJsonArray fields{entry.date.toString(Qt::ISODate), entry.worker.key(),
                            QString::number(entry.customerId), QString::number(entry.itemId),
                            QString::number(entry.minutes), entry.billable ? QStringLiteral("1") : QStringLiteral("0"),
                            entry.notes.trimmed(), QString::number(entry.classId),
                            entry.invoiceId == 0 ? QStringLiteral("open") : QStringLiteral("billed")};
    return QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact));
}

void refuse(Context &ctx, const IifRecord &row, const QString &reason)
{
    ++ctx.report.timeActivities.skipped;
    ctx.report.refused << IifRefusal{row.line, row.line, QStringLiteral("TIMEACT"),
                                    QStringLiteral("line %1: %2").arg(row.line).arg(reason)};
}

void importEmployees(Context &ctx, const QList<IifRecord> &rows, QHash<QString, EmployeeId> &known)
{
    Employees employees(ctx.file);
    for (const Employee &employee : employees.employees(true)) known.insert(lower(employee.name), employee.id);
    for (const IifRecord &row : rows) {
        if (row.type != QLatin1String("EMP")) continue;
        const QString name = employeeName(row);
        if (name.isEmpty()) {
            ++ctx.report.employees.skipped;
            ctx.report.note(QStringLiteral("line %1: an employee without a name was left out.").arg(row.line));
            continue;
        }
        if (row.has(QStringLiteral("SSNO")) || row.has(QStringLiteral("PITEM")))
            ctx.report.note(QStringLiteral("Employee payroll identity and pay setup are not imported from IIF."));
        if (known.contains(lower(name))) {
            ++ctx.report.employees.existing;
            continue;
        }
        Employee employee;
        employee.name = name;
        employee.email = row.value(QStringLiteral("EMAIL"));
        employee.phone = row.value(QStringLiteral("PHONE1"));
        employee.address = address(row, name);
        employee.notes = row.value(QStringLiteral("NOTEPAD"));
        employee.active = !yes(row.value(QStringLiteral("HIDDEN")));
        const Result created = employees.create(employee);
        if (!created.ok) {
            ++ctx.report.employees.skipped;
            ctx.report.note(QStringLiteral("line %1: employee \"%2\" was left out: %3.")
                                .arg(row.line).arg(name, created.error));
            continue;
        }
        known.insert(lower(name), created.id);
        ++ctx.report.employees.added;
    }
}

void importTime(Context &ctx, const QList<IifRecord> &rows, QHash<QString, EmployeeId> &known)
{
    TimeTracking time(ctx.file);
    QHash<QString, int> existing;
    for (const TimeEntry &entry : time.entries()) ++existing[signature(entry)];
    for (const IifRecord &row : rows) {
        if (row.type != QLatin1String("TIMEACT") && row.type != QLatin1String("TIMEACCT")) continue;
        const QString status = row.value(QStringLiteral("BILLINGSTATUS"));
        if (status == QLatin1String("2")) {
            // AGENT-GUARD: the engine derives Billed from a live invoice
            // line. Treating this source row as unbilled could charge it twice.
            refuse(ctx, row, QStringLiteral("already-billed time needs its invoice link and was left out"));
            continue;
        }
        if (status != QLatin1String("0") && status != QLatin1String("1")) {
            refuse(ctx, row, QStringLiteral("the billing status is unknown; expected 0 or 1"));
            continue;
        }
        TimeEntry entry;
        entry.date = IifReader::parseDate(row.value(QStringLiteral("DATE")));
        const auto duration = TimeTracking::parseDuration(row.value(QStringLiteral("DURATION")));
        const QString workerName = row.value(QStringLiteral("EMP"));
        const QString customerName = row.value(QStringLiteral("JOB"));
        const QString itemName = row.value(QStringLiteral("ITEM"));
        if (!entry.date.isValid() || !duration || *duration > 24 * 60 || workerName.isEmpty()) {
            refuse(ctx, row, QStringLiteral("a date, employee/vendor and duration of at most 24:00 are required"));
            continue;
        }
        if (!itemName.isEmpty() && ctx.item(itemName) == 0) {
            refuse(ctx, row, QStringLiteral("the service item \"%1\" is not in this company").arg(itemName));
            continue;
        }
        if (status == QLatin1String("1") && (customerName.isEmpty() || itemName.isEmpty())) {
            refuse(ctx, row, QStringLiteral("billable time needs a customer:job and service item"));
            continue;
        }
        if (yes(row.value(QStringLiteral("XFERTOPAYROLL")))) {
            refuse(ctx, row, QStringLiteral("time transferred to payroll needs its paycheck link and was left out"));
            continue;
        }
        const QString workerKey = lower(workerName);
        if (const EmployeeId id = known.value(workerKey)) entry.worker = Worker::employee(id);
        else if (const qint64 id = ctx.vendors.value(workerKey)) entry.worker = Worker::vendor(id);
        else {
            Employee employee; employee.name = workerName;
            const Result created = Employees(ctx.file).create(employee);
            if (!created.ok) {
                refuse(ctx, row, QStringLiteral("employee \"%1\" could not be added: %2").arg(workerName, created.error));
                continue;
            }
            known.insert(workerKey, created.id);
            ++ctx.report.employees.added;
            entry.worker = Worker::employee(created.id);
        }
        entry.customerId = customerName.isEmpty() ? 0 : ctx.customer(customerName, true);
        if (!customerName.isEmpty() && entry.customerId == 0) {
            refuse(ctx, row, QStringLiteral("the customer:job \"%1\" could not be added").arg(customerName));
            continue;
        }
        entry.itemId = ctx.item(itemName);
        entry.minutes = *duration;
        entry.billable = status == QLatin1String("1");
        entry.notes = row.value(QStringLiteral("NOTE"));
        const QString project = row.value(QStringLiteral("PROJ"));
        entry.classId = project.isEmpty() ? 0 : ctx.klass(project);
        if (!project.isEmpty() && entry.classId == 0) {
            refuse(ctx, row, QStringLiteral("the class \"%1\" could not be added").arg(project));
            continue;
        }
        const QString key = signature(entry);
        if (existing.value(key) > 0) {
            --existing[key];
            ++ctx.report.timeActivities.existing;
            continue;
        }
        const Result created = time.create(entry);
        if (!created.ok) {
            refuse(ctx, row, created.error);
            continue;
        }
        ++ctx.report.timeActivities.added;
        if (!row.value(QStringLiteral("PITEM")).isEmpty())
            ctx.report.note(QStringLiteral("Time-activity payroll items are not imported; duration and billing state are kept."));
    }
}

} // namespace

void importEmployeesAndTime(Context &ctx, const QList<IifRecord> &rows)
{
    QHash<QString, EmployeeId> known;
    importEmployees(ctx, rows, known);
    importTime(ctx, rows, known);
}

} // namespace QindaOffice::Books::IifInternal
