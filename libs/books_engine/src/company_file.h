// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CompanyFile: one business's books in one `.qbooks` SQLite file (D-045).
//
// Every other engine class (ChartOfAccounts, Ledger, Sales, Purchases,
// Banking, Reconciliation, Reports, FinancialAnalysis …) is a lightweight
// view constructed over a CompanyFile&; they hold no state of their own,
// so any number may exist at once.
//
// Storage rules (docs/books.md "Storage"):
//  * foreign_keys ON; journal_mode DELETE (no -wal/-shm sidecars: the
//    company is ONE file a user can copy); synchronous FULL; 5 s busy
//    timeout.
//  * Every mutation runs inside a SqlTransaction and commits immediately —
//    there is no unsaved state. changed() fires after each outermost
//    commit so views can refresh.
//  * A file with a foreign application_id is refused; a newer
//    user_version is refused (ErrorCode::NewerSchema) with a clear message;
//    an older one is backed up to `<file>.pre-v<N>.bak` and migrated.
//
// Ownership/threading: GUI-thread (or one worker) object; the SQLite
// connection is per-instance and not shared across threads.

#include "books_types.h"
#include "company_settings.h"
#include "money.h"

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

#include <memory>

namespace QindaOffice::Books {

class CompanyFile : public QObject {
    Q_OBJECT
public:
    ~CompanyFile() override;

    // Creates a new company file at `path` (refused when it exists), seeds
    // the chart of accounts from `chartTemplateId` (chart_templates.h; empty
    // = "general"), the standard payment terms and the settings.
    [[nodiscard]] static std::unique_ptr<CompanyFile>
    create(const QString &path, const CompanySettings &settings, const QString &chartTemplateId,
           QString *error = nullptr, ErrorCode *code = nullptr);
    // Opens an existing company file (migrating it forward when older).
    [[nodiscard]] static std::unique_ptr<CompanyFile>
    open(const QString &path, QString *error = nullptr, ErrorCode *code = nullptr);
    // Test hook: an empty file at schema `version` without seed data (the
    // migration tests create a v1 file and reopen it).
    [[nodiscard]] static bool createBareAtVersion(const QString &path, int version, QString *error);

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QSqlDatabase database() const;
    [[nodiscard]] int schemaVersion() const;

    // ---- settings ----
    [[nodiscard]] CompanySettings settings() const;
    // Currency cannot change once anything has posted (Invalid).
    Result setSettings(const CompanySettings &settings);
    // Closing: postings dated on/before `date` are refused unless an
    // override with a reason is given (PostOptions). Audited.
    Result setLockDate(const QDate &date, const QString &reason = {});
    [[nodiscard]] QDate lockDate() const;
    [[nodiscard]] QString currency() const { return m_currency; }
    [[nodiscard]] Money money(qint64 minorUnits) const { return Money(minorUnits, m_currency); }
    // Reserves the next number for invoices, estimates, credit memos and
    // sales receipts ("INV-1001"); empty for kinds without a counter.
    [[nodiscard]] QString takeNextNumber(DocKind kind);
    [[nodiscard]] QString peekNextNumber(DocKind kind) const;

    // ---- maintenance ----
    // A consistent single-file copy via VACUUM INTO. Refused when `target`
    // exists. The copy is itself a valid company file.
    Result backupTo(const QString &target) const;
    // PRAGMA integrity_check + foreign_key_check + "every live transaction
    // balances". Empty = healthy.
    [[nodiscard]] QStringList integrityProblems() const;

    // ---- audit ----
    // Who is recorded in audit_log (defaults to $USER).
    [[nodiscard]] QString userName() const { return m_userName; }
    void setUserName(const QString &name) { m_userName = name; }
    // Appends one audit row. JSON arguments are compact JSON text.
    bool audit(const QString &action, const QString &entity, qint64 entityId,
               const QString &beforeJson, const QString &afterJson, const QString &reason = {});

    // ---- transactions (used by the engine modules) ----
    bool beginSavepoint(QString *name, QString *error);
    bool releaseSavepoint(const QString &name, QString *error);
    void rollbackSavepoint(const QString &name);
    [[nodiscard]] static QString nowStamp();

signals:
    // After every outermost committed mutation.
    void changed();

private:
    explicit CompanyFile(const QString &path, const QString &connectionName);
    bool openConnection(QString *error);
    void loadCachedSettings();

    QString m_path;
    QString m_connection;
    QString m_currency;
    QString m_userName;
    int m_depth = 0;
    quint64 m_savepointSeq = 0;
};

// RAII SQL transaction (a SAVEPOINT, so they nest). Rolls back unless
// commit() succeeded.
class SqlTransaction {
public:
    explicit SqlTransaction(CompanyFile &file);
    ~SqlTransaction();
    SqlTransaction(const SqlTransaction &) = delete;
    SqlTransaction &operator=(const SqlTransaction &) = delete;

    [[nodiscard]] bool begun() const { return m_begun; }
    [[nodiscard]] QString error() const { return m_error; }
    bool commit();

private:
    CompanyFile &m_file;
    QString m_name;
    QString m_error;
    bool m_begun = false;
    bool m_done = false;
};

} // namespace QindaOffice::Books
