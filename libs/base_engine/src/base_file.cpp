// SPDX-License-Identifier: GPL-3.0-or-later
// BaseFile: opening, creating, system tables, integrity, backups, settings.
// Changes, plans and images are in base_changes.cpp.
#include "base_file.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace QindaOffice::Base {

void registerBaseFunctions(BaseFile *file); // base_functions.cpp

namespace {

// AGENT-CONTRACT (system tables v1, D-049 §1): names, columns and meaning
// are read by every QindaBase version; add columns, never rename them.
const char *const kSystemSchema[] = {
    "CREATE TABLE IF NOT EXISTS qb_meta(key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE IF NOT EXISTS qb_objects(id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " kind TEXT NOT NULL, name TEXT NOT NULL, definition TEXT NOT NULL, description TEXT,"
    " created TEXT, modified TEXT)",
    "CREATE UNIQUE INDEX IF NOT EXISTS qb_objects_name ON qb_objects(kind, name COLLATE NOCASE)",
    // M80 (D-089) added validation_rule, validation_text and input_mask; an
    // older file gets them by ALTER TABLE ADD COLUMN in the first plan that
    // writes qb_fields (detail::systemColumnStatements). The row with an
    // empty field_name holds the table's own validation rule.
    "CREATE TABLE IF NOT EXISTS qb_fields(table_name TEXT NOT NULL, field_name TEXT NOT NULL, type TEXT,"
    " caption TEXT, description TEXT, lookup_table TEXT, lookup_field TEXT, lookup_display TEXT,"
    " validation_rule TEXT, validation_text TEXT, input_mask TEXT,"
    " PRIMARY KEY(table_name, field_name))",
};

QString nowText()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

bool looksLikeSqlite(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray header = file.read(16);
    if (header != QByteArray("SQLite format 3\0", 16)) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not a SQLite database.").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    return true;
}

} // namespace

QString SchemaPlan::sqlText() const
{
    QString out;
    for (const QString &statement : statements) {
        out += statement + QStringLiteral(";\n");
    }
    return out;
}

BaseFile::~BaseFile() = default;

std::unique_ptr<BaseFile> BaseFile::create(const QString &path, QString *error)
{
    if (QFileInfo::exists(path)) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 already exists.").arg(QFileInfo(path).fileName());
        }
        return nullptr;
    }
    std::unique_ptr<BaseFile> file(new BaseFile);
    if (!file->m_db.open(path, true, false, error)) {
        return nullptr;
    }
    Connection &db = file->m_db;
    Result made = db.exec(QStringLiteral("PRAGMA journal_mode=DELETE; PRAGMA application_id=%1; PRAGMA user_version=%2;")
                              .arg(kApplicationId)
                              .arg(kSchemaVersion));
    if (made.ok) {
        file->m_path = QFileInfo(path).absoluteFilePath();
        made = file->ensureSystemTables();
    }
    if (made.ok) {
        made = db.run(QStringLiteral("INSERT OR REPLACE INTO qb_meta(key, value) VALUES('created', ?)"), {nowText()});
    }
    if (!made.ok) {
        if (error != nullptr) {
            *error = made.error;
        }
        file->m_db.close();
        QFile::remove(path);
        return nullptr;
    }
    if (!file->finishOpen(error)) {
        return nullptr;
    }
    return file;
}

std::unique_ptr<BaseFile> BaseFile::open(const QString &path, QString *error)
{
    if (!looksLikeSqlite(path, error)) {
        return nullptr;
    }
    std::unique_ptr<BaseFile> file(new BaseFile);
    const bool writable = QFileInfo(path).isWritable();
    if (!file->m_db.open(path, false, !writable, error)) {
        return nullptr;
    }
    file->m_readOnly = !writable;
    file->m_path = QFileInfo(path).absoluteFilePath();
    if (!file->finishOpen(error)) {
        return nullptr;
    }
    return file;
}

bool BaseFile::finishOpen(QString *error)
{
    QString problem;
    // A file that is not a database at all fails here, not later.
    const QVariant appId = m_db.scalar(QStringLiteral("PRAGMA application_id"), {}, &problem);
    if (!problem.isEmpty()) {
        if (error != nullptr) {
            *error = problem;
        }
        return false;
    }
    m_isQBase = appId.toLongLong() == kApplicationId;
    registerBaseFunctions(this);
    m_db.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    m_openProblems = integrityProblems(false);
    if (hasSystemTables()) {
        const int version = setting(QStringLiteral("schemaVersion"), QStringLiteral("1")).toInt();
        if (version > kSchemaVersion) {
            m_readOnly = true;
            m_openProblems.append(QStringLiteral("This database was saved by a newer QindaBase (format %1); it is "
                                                 "open read-only.")
                                      .arg(version));
        }
    }
    return true;
}

bool BaseFile::hasSystemTables() const
{
    return m_db.scalar(QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE type='table' AND name IN "
                                      "('qb_meta','qb_objects','qb_fields')"))
               .toInt()
           == 3;
}

Result BaseFile::ensureSystemTables()
{
    if (m_readOnly) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    if (hasSystemTables()) {
        return Result::success();
    }
    Savepoint sp(m_db, QStringLiteral("qb_system"));
    for (const char *sql : kSystemSchema) {
        const Result r = m_db.exec(QString::fromLatin1(sql));
        if (!r.ok) {
            return r;
        }
    }
    const Result version = m_db.run(QStringLiteral("INSERT OR IGNORE INTO qb_meta(key, value) VALUES('schemaVersion', ?)"),
                                    {QString::number(kSchemaVersion)});
    if (!version.ok) {
        return version;
    }
    const Result r = sp.release();
    if (r.ok) {
        ++m_revision;
        emit schemaChanged();
    }
    return r;
}

QStringList BaseFile::integrityProblems(bool full) const
{
    QStringList out;
    QString error;
    const QList<QVariantList> rows =
        m_db.rows(full ? QStringLiteral("PRAGMA integrity_check(50)") : QStringLiteral("PRAGMA quick_check(50)"), {},
                  &error);
    if (!error.isEmpty()) {
        out.append(error);
    }
    for (const QVariantList &row : rows) {
        const QString text = row.value(0).toString();
        if (text != QLatin1String("ok")) {
            out.append(text);
        }
    }
    const QList<QVariantList> orphans = m_db.rows(QStringLiteral("PRAGMA foreign_key_check"));
    if (!orphans.isEmpty()) {
        QMap<QString, int> byTable;
        for (const QVariantList &row : orphans) {
            byTable[row.value(0).toString() + QStringLiteral(" → ") + row.value(2).toString()] += 1;
        }
        for (auto it = byTable.cbegin(); it != byTable.cend(); ++it) {
            out.append(QStringLiteral("%1 record(s) in %2 point to a missing record").arg(it.value()).arg(it.key()));
        }
    }
    return out;
}

Result BaseFile::backupTo(const QString &path) const
{
    if (QFileInfo::exists(path)) {
        return Result::failure(QStringLiteral("%1 already exists.").arg(path));
    }
    QString error;
    Statement statement = m_db.prepare(QStringLiteral("VACUUM INTO ?"), &error);
    if (!statement.isValid()) {
        return Result::failure(error);
    }
    statement.bind(1, path);
    if (statement.step() == Statement::Step::Error) {
        return Result::failure(statement.errorText());
    }
    return Result::success();
}

QString BaseFile::backupDirectory()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/qindaoffice/base/backups");
}

QString BaseFile::automaticBackup(const QString &reason, QString *error) const
{
    const QString dir = backupDirectory();
    if (!QDir().mkpath(dir)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create the backup folder %1.").arg(dir);
        }
        return {};
    }
    const QString base = QFileInfo(m_path).completeBaseName();
    QString slug = reason.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug = slug.left(32);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString target = QStringLiteral("%1/%2-%3-%4.qbase").arg(dir, base, stamp, slug);
    const Result r = backupTo(target);
    if (!r.ok) {
        if (error != nullptr) {
            *error = r.error;
        }
        return {};
    }
    // Keep the newest 20 backups of this database.
    QDir folder(dir);
    QStringList mine = folder.entryList({base + QStringLiteral("-*.qbase")}, QDir::Files, QDir::Name);
    while (mine.size() > 20) {
        folder.remove(mine.takeFirst());
    }
    return target;
}

Result BaseFile::compact(QStringList *problems)
{
    if (m_readOnly) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    if (m_db.inTransaction()) {
        return Result::failure(QStringLiteral("A change is still in progress."));
    }
    const Result vacuum = m_db.exec(QStringLiteral("VACUUM"));
    if (!vacuum.ok) {
        return vacuum;
    }
    const QStringList found = integrityProblems(true);
    if (problems != nullptr) {
        *problems = found;
    }
    ++m_revision;
    return Result::success();
}

bool BaseFile::isSystemName(const QString &name)
{
    return name.startsWith(QLatin1String("qb_"), Qt::CaseInsensitive)
           || name.startsWith(QLatin1String("sqlite_"), Qt::CaseInsensitive);
}

QStringList BaseFile::tableNames() const
{
    QStringList out;
    for (const QVariantList &row : m_db.rows(QStringLiteral("SELECT name FROM sqlite_schema WHERE type='table'"))) {
        const QString name = row.value(0).toString();
        if (!isSystemName(name)) {
            out.append(name);
        }
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return out;
}

QStringList BaseFile::viewNames() const
{
    QStringList out;
    for (const QVariantList &row : m_db.rows(QStringLiteral("SELECT name FROM sqlite_schema WHERE type='view' ORDER BY name"))) {
        out.append(row.value(0).toString());
    }
    return out;
}

bool BaseFile::tableExists(const QString &name) const
{
    return m_db.scalar(QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE type='table' AND name=? COLLATE NOCASE"),
                       {name})
               .toInt()
           > 0;
}

QString BaseFile::setting(const QString &key, const QString &fallback) const
{
    if (!hasSystemTables()) {
        return fallback;
    }
    const QVariant v = m_db.scalar(QStringLiteral("SELECT value FROM qb_meta WHERE key=?"), {key});
    return v.isValid() ? v.toString() : fallback;
}

Result BaseFile::setSetting(const QString &key, const QString &value)
{
    const Result ensured = ensureSystemTables();
    if (!ensured.ok) {
        return ensured;
    }
    m_currency.reset();
    return m_db.run(QStringLiteral("INSERT OR REPLACE INTO qb_meta(key, value) VALUES(?, ?)"), {key, value});
}

CurrencyFormat BaseFile::currency() const
{
    if (m_currency) {
        return *m_currency;
    }
    CurrencyFormat format;
    format.symbol = setting(QStringLiteral("currency.symbol"), format.symbol);
    format.decimals = std::clamp(setting(QStringLiteral("currency.decimals"), QStringLiteral("2")).toInt(), 0, 4);
    m_currency = format;
    return format;
}

} // namespace QindaOffice::Base
