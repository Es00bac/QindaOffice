// SPDX-License-Identifier: GPL-3.0-or-later
#include "company_file.h"

#include "accounts.h"
#include "chart_templates.h"
#include "schema_migrations.h"
#include "sql_util.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace QindaOffice::Books {

namespace {

void setError(QString *error, ErrorCode *code, ErrorCode value, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    if (code != nullptr) {
        *code = value;
    }
}

bool seedTerms(const QSqlDatabase &db, qint64 *net30, QString *error)
{
    const QList<QPair<QString, int>> terms = {
        {QStringLiteral("Due on receipt"), 0}, {QStringLiteral("Net 15"), 15},
        {QStringLiteral("Net 30"), 30},        {QStringLiteral("Net 60"), 60},
    };
    for (const auto &[name, days] : terms) {
        const qint64 id = Sql::insert(db, QStringLiteral("INSERT INTO terms(name, due_days) VALUES(?, ?)"),
                                      {name, days}, error);
        if (id == 0) {
            return false;
        }
        if (days == 30) {
            *net30 = id;
        }
    }
    return true;
}

} // namespace

CompanyFile::CompanyFile(const QString &path, const QString &connectionName)
    : m_path(path)
    , m_connection(connectionName)
    , m_userName(qEnvironmentVariable("USER", QStringLiteral("user")))
{
}

CompanyFile::~CompanyFile()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connection);
}

QSqlDatabase CompanyFile::database() const
{
    return QSqlDatabase::database(m_connection, false);
}

bool CompanyFile::openConnection(QString *error)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        if (error != nullptr) {
            *error = db.lastError().text();
        }
        return false;
    }
    // AGENT-GUARD: DELETE journal mode keeps the company ONE file (WAL
    // would add -wal/-shm sidecars a user copying the file would miss).
    return Sql::exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), {}, error)
        && Sql::exec(db, QStringLiteral("PRAGMA journal_mode = DELETE"), {}, error)
        && Sql::exec(db, QStringLiteral("PRAGMA synchronous = FULL"), {}, error);
}

std::unique_ptr<CompanyFile> CompanyFile::create(const QString &path, const CompanySettings &settings,
                                                 const QString &chartTemplateId, QString *error,
                                                 ErrorCode *code)
{
    if (QFileInfo::exists(path)) {
        setError(error, code, ErrorCode::Duplicate, QStringLiteral("%1 already exists").arg(path));
        return nullptr;
    }
    if (settings.legalName.trimmed().isEmpty()) {
        setError(error, code, ErrorCode::Invalid, QStringLiteral("a company needs a legal name"));
        return nullptr;
    }
    const QString currency = settings.currency.trimmed().toUpper();
    if (currency.size() != 3) {
        setError(error, code, ErrorCode::Invalid,
                 QStringLiteral("currency must be an ISO 4217 code, got \"%1\"").arg(settings.currency));
        return nullptr;
    }
    std::unique_ptr<CompanyFile> file(
        new CompanyFile(path, QStringLiteral("qbooks-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString message;
    if (!file->openConnection(&message)) {
        setError(error, code, ErrorCode::Storage, message);
        return nullptr;
    }
    const QSqlDatabase db = file->database();
    bool ok = Sql::exec(db, QStringLiteral("BEGIN"), {}, &message)
        && Schema::create(db, Schema::kCurrentVersion, &message);
    qint64 net30 = 0;
    if (ok) {
        file->m_currency = currency;
        ok = applyChartTemplate(*file, chartTemplateId.isEmpty() ? QStringLiteral("general") : chartTemplateId,
                                &message)
            && seedTerms(db, &net30, &message);
    }
    if (ok) {
        const Result roles = ChartOfAccounts(*file).ensureSystemAccounts();
        ok = roles.ok;
        message = roles.error;
    }
    if (ok) {
        CompanySettings seeded = settings;
        seeded.currency = currency;
        if (seeded.defaultTermsId == 0) {
            seeded.defaultTermsId = net30;
        }
        const Result written = file->setSettings(seeded);
        ok = written.ok;
        message = written.error;
    }
    ok = ok && file->audit(QStringLiteral("create"), QStringLiteral("company"), 0, {},
                           QStringLiteral("{\"template\":\"%1\"}").arg(chartTemplateId));
    if (!ok || !Sql::exec(db, QStringLiteral("COMMIT"), {}, &message)) {
        Sql::exec(db, QStringLiteral("ROLLBACK"));
        file.reset();
        QFile::remove(path);
        setError(error, code, ErrorCode::Storage, message);
        return nullptr;
    }
    file->loadCachedSettings();
    return file;
}

bool CompanyFile::createBareAtVersion(const QString &path, int version, QString *error)
{
    CompanyFile file(path, QStringLiteral("qbooks-bare-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    return file.openConnection(error) && Schema::create(file.database(), version, error);
}

std::unique_ptr<CompanyFile> CompanyFile::open(const QString &path, QString *error, ErrorCode *code)
{
    if (!QFileInfo(path).isFile()) {
        setError(error, code, ErrorCode::NotFound, QStringLiteral("%1 does not exist").arg(path));
        return nullptr;
    }
    std::unique_ptr<CompanyFile> file(
        new CompanyFile(path, QStringLiteral("qbooks-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString message;
    if (!file->openConnection(&message)) {
        setError(error, code, ErrorCode::NotACompanyFile,
                 QStringLiteral("%1 is not a QindaBooks company file (%2)").arg(path, message));
        return nullptr;
    }
    const QSqlDatabase db = file->database();
    const int appId = Schema::readApplicationId(db);
    const int version = Schema::readUserVersion(db);
    if (appId != Schema::kApplicationId) {
        setError(error, code, ErrorCode::NotACompanyFile,
                 QStringLiteral("%1 is not a QindaBooks company file").arg(path));
        return nullptr;
    }
    if (version > Schema::kCurrentVersion) {
        setError(error, code, ErrorCode::NewerSchema,
                 QStringLiteral("%1 was written by a newer version of QindaBooks (company file "
                                "version %2; this version reads up to %3). Update QindaBooks to open it.")
                     .arg(path)
                     .arg(version)
                     .arg(Schema::kCurrentVersion));
        return nullptr;
    }
    if (version < Schema::kCurrentVersion) {
        // AGENT-NOTE: a copy of the file as it was goes next to it before
        // the first migration step, so a failed or regretted upgrade never
        // costs the user their books.
        const QString backup = path + QStringLiteral(".pre-v%1.bak").arg(Schema::kCurrentVersion);
        if (!QFileInfo::exists(backup)) {
            if (const Result copied = file->backupTo(backup); !copied.ok) {
                setError(error, code, ErrorCode::Storage, copied.error);
                return nullptr;
            }
        }
        const bool migrated = Sql::exec(db, QStringLiteral("BEGIN"), {}, &message)
            && Schema::migrate(db, version, &message)
            && file->audit(QStringLiteral("migrate"), QStringLiteral("schema"), Schema::kCurrentVersion,
                           QStringLiteral("{\"version\":%1}").arg(version),
                           QStringLiteral("{\"version\":%1}").arg(Schema::kCurrentVersion))
            && Sql::exec(db, QStringLiteral("COMMIT"), {}, &message);
        if (!migrated) {
            Sql::exec(db, QStringLiteral("ROLLBACK"));
            setError(error, code, ErrorCode::Storage,
                     QStringLiteral("could not upgrade %1: %2").arg(path, message));
            return nullptr;
        }
    }
    file->loadCachedSettings();
    return file;
}

int CompanyFile::schemaVersion() const
{
    return Schema::readUserVersion(database());
}

void CompanyFile::loadCachedSettings()
{
    m_currency = settings().currency;
}

QString CompanyFile::nowStamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

// ---- maintenance ------------------------------------------------------------

Result CompanyFile::backupTo(const QString &target) const
{
    if (QFileInfo::exists(target)) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("%1 already exists").arg(target));
    }
    QString error;
    if (!Sql::exec(database(), QStringLiteral("VACUUM INTO ?"), {target}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    return Result::success();
}

QStringList CompanyFile::integrityProblems() const
{
    QStringList problems;
    const QSqlDatabase db = database();
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("PRAGMA integrity_check"));
        while (q.next()) {
            if (q.value(0).toString() != QLatin1String("ok")) {
                problems << q.value(0).toString();
            }
        }
    }
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("PRAGMA foreign_key_check"));
        while (q.next()) {
            problems << QStringLiteral("foreign key: %1 row %2 -> %3")
                            .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString());
        }
    }
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT txn_id, SUM(debit), SUM(credit) FROM journal_lines"
                                   " WHERE voided = 0 GROUP BY txn_id HAVING SUM(debit) <> SUM(credit)"));
        while (q.next()) {
            problems << QStringLiteral("transaction %1 does not balance").arg(q.value(0).toLongLong());
        }
    }
    return problems;
}

// ---- audit ------------------------------------------------------------------

bool CompanyFile::audit(const QString &action, const QString &entity, qint64 entityId,
                        const QString &beforeJson, const QString &afterJson, const QString &reason)
{
    return Sql::insert(database(),
                       QStringLiteral("INSERT INTO audit_log(at, user, action, entity, entity_id,"
                                      " before_json, after_json, reason) VALUES(?,?,?,?,?,?,?,?)"),
                       {nowStamp(), m_userName, action, entity, entityId,
                        beforeJson.isEmpty() ? QVariant() : QVariant(beforeJson),
                        afterJson.isEmpty() ? QVariant() : QVariant(afterJson),
                        reason.isEmpty() ? QVariant() : QVariant(reason)})
        != 0;
}

// ---- savepoints ---------------------------------------------------------------

bool CompanyFile::beginSavepoint(QString *name, QString *error)
{
    *name = QStringLiteral("qb_sp_%1").arg(++m_savepointSeq);
    if (!Sql::exec(database(), QStringLiteral("SAVEPOINT ") + *name, {}, error)) {
        return false;
    }
    ++m_depth;
    return true;
}

bool CompanyFile::releaseSavepoint(const QString &name, QString *error)
{
    if (!Sql::exec(database(), QStringLiteral("RELEASE SAVEPOINT ") + name, {}, error)) {
        return false;
    }
    if (--m_depth == 0) {
        emit changed();
    }
    return true;
}

void CompanyFile::rollbackSavepoint(const QString &name)
{
    Sql::exec(database(), QStringLiteral("ROLLBACK TO SAVEPOINT ") + name);
    Sql::exec(database(), QStringLiteral("RELEASE SAVEPOINT ") + name);
    --m_depth;
}

SqlTransaction::SqlTransaction(CompanyFile &file)
    : m_file(file)
{
    m_begun = m_file.beginSavepoint(&m_name, &m_error);
}

SqlTransaction::~SqlTransaction()
{
    if (m_begun && !m_done) {
        m_file.rollbackSavepoint(m_name);
    }
}

bool SqlTransaction::commit()
{
    if (!m_begun || m_done) {
        return false;
    }
    if (!m_file.releaseSavepoint(m_name, &m_error)) {
        return false; // the destructor rolls back
    }
    m_done = true;
    return true;
}

} // namespace QindaOffice::Books
