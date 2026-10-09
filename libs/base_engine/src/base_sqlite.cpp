// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_sqlite.h"

#include <sqlite3.h>

#include <algorithm>
#include <utility>

namespace QindaOffice::Base {

namespace {

QString text(const char *utf8)
{
    return utf8 != nullptr ? QString::fromUtf8(utf8) : QString();
}

} // namespace

// ---- Statement -------------------------------------------------------------

Statement::Statement(sqlite3 *db, const QString &sql, QString *error, QString *tail)
    : m_db(db)
{
    const QByteArray utf8 = sql.toUtf8();
    const char *rest = nullptr;
    const int rc = sqlite3_prepare_v2(db, utf8.constData(), int(utf8.size()), &m_stmt, &rest);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            *error = text(sqlite3_errmsg(db));
        }
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
        return;
    }
    if (m_stmt == nullptr && error != nullptr) {
        *error = QStringLiteral("The SQL holds no statement.");
    }
    if (tail != nullptr) {
        *tail = rest != nullptr ? QString::fromUtf8(rest) : QString();
    }
}

Statement::~Statement()
{
    sqlite3_finalize(m_stmt);
}

Statement::Statement(Statement &&other) noexcept
    : m_db(std::exchange(other.m_db, nullptr))
    , m_stmt(std::exchange(other.m_stmt, nullptr))
{
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this != &other) {
        sqlite3_finalize(m_stmt);
        m_db = std::exchange(other.m_db, nullptr);
        m_stmt = std::exchange(other.m_stmt, nullptr);
    }
    return *this;
}

bool Statement::bind(int index, const QVariant &value)
{
    if (m_stmt == nullptr) {
        return false;
    }
    int rc = SQLITE_OK;
    if (!value.isValid() || value.isNull()) {
        rc = sqlite3_bind_null(m_stmt, index);
    } else {
        switch (value.metaType().id()) {
        case QMetaType::Bool:
            rc = sqlite3_bind_int64(m_stmt, index, value.toBool() ? 1 : 0);
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
            rc = sqlite3_bind_int64(m_stmt, index, value.toLongLong());
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            rc = sqlite3_bind_double(m_stmt, index, value.toDouble());
            break;
        case QMetaType::QByteArray: {
            const QByteArray bytes = value.toByteArray();
            rc = sqlite3_bind_blob64(m_stmt, index, bytes.constData(), sqlite3_uint64(bytes.size()),
                                     SQLITE_TRANSIENT);
            break;
        }
        default: {
            const QByteArray utf8 = value.toString().toUtf8();
            rc = sqlite3_bind_text64(m_stmt, index, utf8.constData(), sqlite3_uint64(utf8.size()),
                                     SQLITE_TRANSIENT, SQLITE_UTF8);
            break;
        }
        }
    }
    return rc == SQLITE_OK;
}

bool Statement::bindAll(const QVariantList &values)
{
    for (int i = 0; i < values.size(); ++i) {
        if (!bind(i + 1, values.at(i))) {
            return false;
        }
    }
    return true;
}

int Statement::parameterCount() const
{
    return m_stmt != nullptr ? sqlite3_bind_parameter_count(m_stmt) : 0;
}

QString Statement::parameterName(int index) const
{
    return m_stmt != nullptr ? text(sqlite3_bind_parameter_name(m_stmt, index)) : QString();
}

Statement::Step Statement::step()
{
    if (m_stmt == nullptr) {
        return Step::Error;
    }
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) {
        return Step::Row;
    }
    return rc == SQLITE_DONE ? Step::Done : Step::Error;
}

void Statement::reset()
{
    if (m_stmt != nullptr) {
        sqlite3_reset(m_stmt);
        sqlite3_clear_bindings(m_stmt);
    }
}

int Statement::columnCount() const
{
    return m_stmt != nullptr ? sqlite3_column_count(m_stmt) : 0;
}

QString Statement::columnName(int column) const
{
    return text(sqlite3_column_name(m_stmt, column));
}

QString Statement::declaredType(int column) const
{
    return text(sqlite3_column_decltype(m_stmt, column));
}

QString Statement::originTable(int column) const
{
    return text(sqlite3_column_table_name(m_stmt, column));
}

QString Statement::originColumn(int column) const
{
    return text(sqlite3_column_origin_name(m_stmt, column));
}

QVariant Statement::value(int column) const
{
    switch (sqlite3_column_type(m_stmt, column)) {
    case SQLITE_INTEGER:
        return QVariant::fromValue<qint64>(sqlite3_column_int64(m_stmt, column));
    case SQLITE_FLOAT:
        return sqlite3_column_double(m_stmt, column);
    case SQLITE_TEXT: {
        const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column));
        return QString::fromUtf8(data, sqlite3_column_bytes(m_stmt, column));
    }
    case SQLITE_BLOB: {
        const auto *data = static_cast<const char *>(sqlite3_column_blob(m_stmt, column));
        return QByteArray(data, sqlite3_column_bytes(m_stmt, column));
    }
    default:
        return {};
    }
}

bool Statement::isReadOnly() const
{
    return m_stmt != nullptr && sqlite3_stmt_readonly(m_stmt) != 0;
}

QString Statement::errorText() const
{
    return m_db != nullptr ? text(sqlite3_errmsg(m_db)) : QString();
}

// ---- Connection ------------------------------------------------------------

Connection::~Connection()
{
    close();
}

bool Connection::open(const QString &path, bool create, bool readOnly, QString *error)
{
    close();
    int flags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (create && !readOnly) {
        flags |= SQLITE_OPEN_CREATE;
    }
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            *error = m_db != nullptr ? text(sqlite3_errmsg(m_db)) : QStringLiteral("Cannot open the database.");
        }
        close();
        return false;
    }
    sqlite3_extended_result_codes(m_db, 1);
    sqlite3_busy_timeout(m_db, 3000);
    return true;
}

bool Connection::openMemory(QString *error)
{
    return open(QStringLiteral(":memory:"), true, false, error);
}

void Connection::close()
{
    if (m_db != nullptr) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

Result Connection::exec(const QString &sql)
{
    char *message = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        const QString error = message != nullptr ? QString::fromUtf8(message) : lastError();
        sqlite3_free(message);
        return Result::failure(error);
    }
    return Result::success();
}

Result Connection::run(const QString &sql, const QVariantList &bindings)
{
    QString error;
    Statement statement(m_db, sql, &error);
    if (!statement.isValid()) {
        return Result::failure(error);
    }
    if (!statement.bindAll(bindings)) {
        return Result::failure(statement.errorText());
    }
    Statement::Step step = statement.step();
    while (step == Statement::Step::Row) {
        step = statement.step();
    }
    if (step == Statement::Step::Error) {
        return Result::failure(statement.errorText());
    }
    return Result::success();
}

QVariant Connection::scalar(const QString &sql, const QVariantList &bindings, QString *error) const
{
    Statement statement(m_db, sql, error);
    if (!statement.isValid() || !statement.bindAll(bindings)) {
        return {};
    }
    const Statement::Step step = statement.step();
    if (step == Statement::Step::Error && error != nullptr) {
        *error = statement.errorText();
    }
    return step == Statement::Step::Row ? statement.value(0) : QVariant();
}

QList<QVariantList> Connection::rows(const QString &sql, const QVariantList &bindings, QString *error) const
{
    QList<QVariantList> out;
    Statement statement(m_db, sql, error);
    if (!statement.isValid() || !statement.bindAll(bindings)) {
        return out;
    }
    const int columns = statement.columnCount();
    Statement::Step step = statement.step();
    while (step == Statement::Step::Row) {
        QVariantList row;
        row.reserve(columns);
        for (int c = 0; c < columns; ++c) {
            row.append(statement.value(c));
        }
        out.append(row);
        step = statement.step();
    }
    if (step == Statement::Step::Error && error != nullptr) {
        *error = statement.errorText();
    }
    return out;
}

Statement Connection::prepare(const QString &sql, QString *error) const
{
    return Statement(m_db, sql, error);
}

QByteArray Connection::blobHead(const QString &table, const QString &column, qint64 rowid, int bytes) const
{
    sqlite3_blob *blob = nullptr;
    if (m_db == nullptr
        || sqlite3_blob_open(m_db, "main", table.toUtf8().constData(), column.toUtf8().constData(), rowid, 0, &blob)
               != SQLITE_OK) {
        sqlite3_blob_close(blob); // a failed open may still hand back a handle (NULL is a no-op)
        return {};
    }
    QByteArray out(std::min(bytes, sqlite3_blob_bytes(blob)), Qt::Uninitialized);
    if (!out.isEmpty() && sqlite3_blob_read(blob, out.data(), int(out.size()), 0) != SQLITE_OK) {
        out.clear();
    }
    sqlite3_blob_close(blob);
    return out;
}

qint64 Connection::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_db);
}

qint64 Connection::changes() const
{
    return sqlite3_changes64(m_db);
}

QString Connection::lastError() const
{
    return m_db != nullptr ? text(sqlite3_errmsg(m_db)) : QStringLiteral("The database is closed.");
}

bool Connection::inTransaction() const
{
    return m_db != nullptr && sqlite3_get_autocommit(m_db) == 0;
}

// ---- Savepoint -------------------------------------------------------------

Savepoint::Savepoint(Connection &db, const QString &name)
    : m_db(db)
    , m_name(name)
{
    const Result begun = m_db.exec(QStringLiteral("SAVEPOINT ") + quoteIdentifier(m_name));
    m_active = begun.ok;
    m_error = begun.error;
}

Savepoint::~Savepoint()
{
    rollback();
}

Result Savepoint::release()
{
    if (!m_active) {
        return Result::failure(m_error.isEmpty() ? QStringLiteral("No open savepoint.") : m_error);
    }
    const Result released = m_db.exec(QStringLiteral("RELEASE ") + quoteIdentifier(m_name));
    if (released.ok) {
        m_active = false;
    }
    return released;
}

void Savepoint::rollback()
{
    if (!m_active) {
        return;
    }
    const QString name = quoteIdentifier(m_name);
    m_db.exec(QStringLiteral("ROLLBACK TO ") + name);
    m_db.exec(QStringLiteral("RELEASE ") + name);
    m_active = false;
}

// ---- helpers ---------------------------------------------------------------

QString quoteIdentifier(const QString &name)
{
    QString out = name;
    out.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString quoteLiteral(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

QString sqlLiteral(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return QStringLiteral("NULL");
    }
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QString::number(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return QString::number(value.toDouble(), 'g', 17);
    case QMetaType::QByteArray:
        return QStringLiteral("X'") + QString::fromLatin1(value.toByteArray().toHex()) + QLatin1Char('\'');
    default:
        return quoteLiteral(value.toString());
    }
}

QStringList splitStatements(const QString &sql, QString *error)
{
    QStringList out;
    QString current;
    for (const QChar ch : sql) {
        current.append(ch);
        if (ch == QLatin1Char(';') && sqlite3_complete(current.toUtf8().constData()) != 0) {
            QString statement = current.trimmed();
            statement.chop(1);
            statement = statement.trimmed();
            if (!statement.isEmpty()) {
                out.append(statement);
            }
            current.clear();
        }
    }
    const QString rest = current.trimmed();
    if (!rest.isEmpty()) {
        // A final statement without its semicolon is still one statement,
        // unless it is unterminated (an open string or comment).
        if (sqlite3_complete((rest + QLatin1Char(';')).toUtf8().constData()) == 0) {
            if (error != nullptr) {
                *error = QStringLiteral("The SQL ends in the middle of a statement.");
            }
        } else {
            // Comments alone are not statements.
            if (!firstKeyword(rest).isEmpty()) {
                out.append(rest);
            }
        }
    }
    return out;
}

QString firstKeyword(const QString &sql)
{
    int i = 0;
    const int n = int(sql.size());
    while (i < n) {
        if (sql.at(i).isSpace()) {
            ++i;
        } else if (sql.mid(i, 2) == QStringLiteral("--")) {
            while (i < n && sql.at(i) != QLatin1Char('\n')) {
                ++i;
            }
        } else if (sql.mid(i, 2) == QStringLiteral("/*")) {
            const int end = int(sql.indexOf(QStringLiteral("*/"), i + 2));
            i = end < 0 ? n : end + 2;
        } else {
            break;
        }
    }
    int j = i;
    while (j < n && (sql.at(j).isLetter() || sql.at(j) == QLatin1Char('_'))) {
        ++j;
    }
    return sql.mid(i, j - i).toUpper();
}

} // namespace QindaOffice::Base
