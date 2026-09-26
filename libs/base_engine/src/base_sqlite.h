// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A thin RAII layer over the SQLite C API for QindaBase (M49, D-049).
//
// AGENT-NOTE (why not Qt Sql): the engine needs what QSQLITE hides — the
// session extension (undo of any data change, cascades included, as one
// changeset), sqlite3_serialize/backup (exact schema-change undo),
// sqlite3_stmt_readonly (telling action queries from selects before they
// run), bind-parameter names (parameter queries) and column origin
// metadata (formatting a query's currency column as money). The library is
// dev-db/sqlite, which QindaTK already links; CMake checks the session API.
//
// Values cross as QVariant: NULL ↔ invalid QVariant, INTEGER ↔ qint64,
// REAL ↔ double, TEXT ↔ QString, BLOB ↔ QByteArray. bool binds as 0/1.
// Nothing here throws; failures return false / Result with SQLite's text.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

struct sqlite3;
struct sqlite3_stmt;

namespace QindaOffice::Base {

struct Result {
    bool ok = true;
    QString error;
    static Result success() { return {}; }
    static Result failure(const QString &message) { return {false, message}; }
    explicit operator bool() const { return ok; }
};

class Statement {
public:
    enum class Step { Row, Done, Error };

    Statement() = default;
    // Prepares exactly the first statement of `sql`; `tail` (optional)
    // receives whatever text follows it.
    Statement(sqlite3 *db, const QString &sql, QString *error, QString *tail = nullptr);
    ~Statement();
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    [[nodiscard]] bool isValid() const { return m_stmt != nullptr; }
    bool bind(int index, const QVariant &value); // 1-based
    bool bindAll(const QVariantList &values);
    [[nodiscard]] int parameterCount() const;
    [[nodiscard]] QString parameterName(int index) const; // 1-based; "" for "?"
    Step step();
    void reset();
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QString columnName(int column) const;
    [[nodiscard]] QString declaredType(int column) const;
    // The table and column a result column reads directly (empty for
    // expressions).
    [[nodiscard]] QString originTable(int column) const;
    [[nodiscard]] QString originColumn(int column) const;
    [[nodiscard]] QVariant value(int column) const;
    [[nodiscard]] bool isReadOnly() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] sqlite3_stmt *handle() const { return m_stmt; }

private:
    sqlite3 *m_db = nullptr;
    sqlite3_stmt *m_stmt = nullptr;
};

class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    bool open(const QString &path, bool create, bool readOnly, QString *error);
    // An in-memory database (tests, scratch).
    bool openMemory(QString *error);
    void close();
    [[nodiscard]] bool isOpen() const { return m_db != nullptr; }
    [[nodiscard]] sqlite3 *handle() const { return m_db; }

    // Runs every statement of `sql` (internal, trusted SQL only).
    Result exec(const QString &sql);
    // Runs exactly one statement with bindings.
    Result run(const QString &sql, const QVariantList &bindings = {});
    [[nodiscard]] QVariant scalar(const QString &sql, const QVariantList &bindings = {},
                                  QString *error = nullptr) const;
    [[nodiscard]] QList<QVariantList> rows(const QString &sql, const QVariantList &bindings = {},
                                           QString *error = nullptr) const;
    [[nodiscard]] Statement prepare(const QString &sql, QString *error = nullptr) const;
    [[nodiscard]] qint64 lastInsertRowId() const;
    [[nodiscard]] qint64 changes() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] bool inTransaction() const;

private:
    sqlite3 *m_db = nullptr;
};

// SAVEPOINT guard: rolls back unless release() succeeded. Nests.
class Savepoint {
public:
    explicit Savepoint(Connection &db, const QString &name = QStringLiteral("qb"));
    ~Savepoint();
    Savepoint(const Savepoint &) = delete;
    Savepoint &operator=(const Savepoint &) = delete;
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] QString error() const { return m_error; }
    Result release();
    void rollback();

private:
    Connection &m_db;
    QString m_name;
    QString m_error;
    bool m_active = false;
};

[[nodiscard]] QString quoteIdentifier(const QString &name);
[[nodiscard]] QString quoteLiteral(const QString &text);
// A value as an SQL literal (NULL, integer, real, 'text', X'blob').
[[nodiscard]] QString sqlLiteral(const QVariant &value);
// Splits a script into complete statements (sqlite3_complete), trimmed,
// empty ones dropped. `error` is set when the text ends mid-statement.
[[nodiscard]] QStringList splitStatements(const QString &sql, QString *error = nullptr);
// The first keyword of a statement, upper-cased ("SELECT", "WITH", …).
[[nodiscard]] QString firstKeyword(const QString &sql);

} // namespace QindaOffice::Base
