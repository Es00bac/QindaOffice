// SPDX-License-Identifier: GPL-3.0-or-later
#include "payroll_tables.h"

#include "payroll_tables_reader_p.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

// AGENT-NOTE: books_payroll is a static library, so its resources need an
// explicit Q_INIT_RESOURCE from inside the library (the linker would
// otherwise drop the unreferenced initializer). Must be outside any
// namespace; the same pattern as books_engine's chart templates.
static void initPayrollResources()
{
    Q_INIT_RESOURCE(payroll_tables);
}

namespace QindaOffice::Books::Payroll {

namespace {

const QString kResourceDir = QStringLiteral(":/books_payroll");

void ensureResources()
{
    static const bool initialised = [] {
        initPayrollResources();
        return true;
    }();
    Q_UNUSED(initialised);
}

void setError(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
}

// "colorado.opt" touches "colorado.opt.sheridan" and the reverse.
bool touches(const QString &listed, const QString &path)
{
    if (listed == path) {
        return true;
    }
    return path.startsWith(listed + QLatin1Char('.')) || listed.startsWith(path + QLatin1Char('.'));
}

} // namespace

std::optional<PayrollTables> PayrollTables::forYear(int year, QString *error)
{
    ensureResources();
    QFile file(QStringLiteral("%1/payroll_%2.json").arg(kResourceDir).arg(year));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, missingYearMessage(year));
        return std::nullopt;
    }
    QString why;
    std::optional<PayrollTables> tables = fromJson(file.readAll(), &why);
    if (!tables) {
        setError(error, QStringLiteral("The %1 payroll tables are damaged (%2); payroll and estimated taxes for %1 "
                                       "cannot be computed.")
                            .arg(year)
                            .arg(why));
        return std::nullopt;
    }
    if (tables->year() != year) {
        setError(error, QStringLiteral("The %1 payroll table file holds %2's numbers.").arg(year).arg(tables->year()));
        return std::nullopt;
    }
    return tables;
}

std::optional<PayrollTables> PayrollTables::fromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(error, QStringLiteral("not a JSON object: %1").arg(parse.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toInt() != kSchemaVersion) {
        setError(error, QStringLiteral("schema %1, this QindaBooks reads schema %2")
                            .arg(root.value(QStringLiteral("schema")).toInt())
                            .arg(kSchemaVersion));
        return std::nullopt;
    }
    PayrollTables tables;
    TablesReader reader(root);
    if (!reader.read(tables)) {
        setError(error, reader.error());
        return std::nullopt;
    }
    return tables;
}

QList<int> PayrollTables::availableYears()
{
    ensureResources();
    static const QRegularExpression name(QStringLiteral("^payroll_(\\d{4})\\.json$"));
    QList<int> years;
    for (const QString &entry : QDir(kResourceDir).entryList(QDir::Files)) {
        const QRegularExpressionMatch m = name.match(entry);
        if (m.hasMatch()) {
            years << m.captured(1).toInt();
        }
    }
    std::sort(years.begin(), years.end());
    return years;
}

QString PayrollTables::missingYearMessage(int year)
{
    return QStringLiteral("QindaBooks has no %1 payroll tax tables, so it cannot compute %1 paychecks or estimated "
                          "taxes. An update that adds the %1 tables (IRS Publication 15-T and 15, Form 1040-ES, "
                          "Colorado DR 1098 and the %1 Colorado rates) is needed first.")
        .arg(year);
}

bool PayrollTables::isConfirmed(const QString &path) const
{
    return std::none_of(m_unconfirmed.cbegin(), m_unconfirmed.cend(),
                        [&](const UnconfirmedValue &u) { return touches(u.path, path); });
}

QList<UnconfirmedValue> PayrollTables::unconfirmedUnder(const QString &path) const
{
    QList<UnconfirmedValue> out;
    for (const UnconfirmedValue &u : m_unconfirmed) {
        if (touches(u.path, path)) {
            out << u;
        }
    }
    return out;
}

const OccupationalPrivilegeTax *PayrollTables::opt(const QString &key) const
{
    for (const OccupationalPrivilegeTax &city : m_colorado.opt) {
        if (city.key == key) {
            return &city;
        }
    }
    return nullptr;
}

} // namespace QindaOffice::Books::Payroll
