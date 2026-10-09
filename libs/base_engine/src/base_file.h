// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BaseFile: one QindaBase database — a single SQLite file (M49, D-049).
//
// AGENT-CONTRACT (file format, D-049 §1):
//  * A `.qbase` file is an ordinary SQLite 3 database with
//    `PRAGMA application_id = 0x51424153` ("QBAS", what the MIME magic
//    matches) and journal_mode DELETE (one copyable file, no -wal).
//  * The user's tables are ordinary tables. QindaBase's own state lives in
//    system tables prefixed `qb_`: qb_meta (settings, schemaVersion),
//    qb_objects (saved queries, forms and reports as JSON) and qb_fields
//    (captions, descriptions, lookups). Unknown qb_meta keys and unknown
//    JSON members are preserved; a newer schemaVersion is opened read-only.
//  * A plain SQLite file (.sqlite/.db) opens as it is: nothing is written
//    until the user saves a design, and then only after consenting to
//    ensureSystemTables(). Its application_id and user_version are never
//    touched.
//  * Every mutation commits immediately (Access semantics). Undo is a
//    Change: a session changeset for data (cascades included) or a pair of
//    whole-database images for schema changes; applyChange replays it.
//  * Integrity: quick_check on open (problems are reported, the file still
//    opens so the user can export what survives); integrity_check + VACUUM
//    in compact(); an automatic backup (VACUUM INTO) before every
//    destructive schema change.
//
// Threading: GUI thread only (one connection, no worker).

#include "base_sqlite.h"
#include "base_types.h"

#include <QObject>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace QindaOffice::Base {

struct Change {
    enum class Kind { Data, Schema };
    Kind kind = Kind::Data;
    QString label;
    QByteArray changeset;     // Data
    QByteArray before, after; // Schema: whole-database images (empty: too large to keep)
    QString backupPath;       // Schema: the automatic backup, when one was made
    [[nodiscard]] bool isEmpty() const { return changeset.isEmpty() && before.isEmpty() && after.isEmpty(); }
    [[nodiscard]] bool undoable() const
    {
        return kind == Kind::Data ? !changeset.isEmpty() : (!before.isEmpty() && !after.isEmpty());
    }
};

// A schema change as ordered single statements (previewable text).
struct SchemaPlan {
    QString label;
    QStringList statements;
    bool destructive = false; // drops columns/tables or loses precision → backup first
    bool legacyAlter = false; // table rebuilds rename with legacy_alter_table=ON
    QStringList notes;        // human-readable summary lines
    // Checks run after the statements inside the transaction: a count query
    // and the warning shown when it is non-zero ("%1" = the count).
    QList<QPair<QString, QString>> checks;
    // M80: checks that REFUSE the change when their count is not zero
    // (a new validation rule existing records break; base_rules.h).
    QList<QPair<QString, QString>> refusals;
    QString error;            // planning refused (validation)
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
    [[nodiscard]] QString sqlText() const;
};

struct PlanPreview {
    bool ok = false;
    QString error;
    QStringList warnings;
};

class BaseFile : public QObject {
    Q_OBJECT
public:
    static constexpr qint32 kApplicationId = 0x51424153; // "QBAS"
    static constexpr int kSchemaVersion = 1;
    static constexpr qint64 kUndoImageLimit = qint64(64) * 1024 * 1024;

    ~BaseFile() override;

    // A new .qbase file at `path` (must not exist).
    static std::unique_ptr<BaseFile> create(const QString &path, QString *error);
    // An existing SQLite file (.qbase or any other).
    static std::unique_ptr<BaseFile> open(const QString &path, QString *error);

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] Connection &db() { return m_db; }
    [[nodiscard]] const Connection &db() const { return m_db; }
    [[nodiscard]] bool isQBase() const { return m_isQBase; }
    [[nodiscard]] bool readOnly() const { return m_readOnly; }
    [[nodiscard]] bool hasSystemTables() const;
    Result ensureSystemTables();
    // quick_check (or integrity_check when `full`) plus foreign-key orphans.
    [[nodiscard]] QStringList integrityProblems(bool full) const;
    [[nodiscard]] QStringList openProblems() const { return m_openProblems; }

    // Copies: VACUUM INTO a path that must not exist.
    Result backupTo(const QString &path) const;
    // A timestamped copy in the backup folder
    // ($XDG_STATE_HOME/qindaoffice/base/backups), keeping the newest 20 per
    // database; returns its path or "" with `error`.
    QString automaticBackup(const QString &reason, QString *error = nullptr) const;
    [[nodiscard]] static QString backupDirectory();
    // VACUUM, then integrity_check; `problems` receives what it found.
    Result compact(QStringList *problems);

    [[nodiscard]] QStringList tableNames() const; // user tables, sorted, no sqlite_/qb_
    [[nodiscard]] QStringList viewNames() const;
    [[nodiscard]] bool tableExists(const QString &name) const;
    [[nodiscard]] static bool isSystemName(const QString &name);

    [[nodiscard]] QString setting(const QString &key, const QString &fallback = {}) const;
    Result setSetting(const QString &key, const QString &value);
    [[nodiscard]] CurrencyFormat currency() const;

    // ---- changes (undo/redo) ----
    // Runs `body` inside a savepoint under a session; on success `change`
    // holds the changeset (empty when nothing changed).
    Result capture(const QString &label, const std::function<Result()> &body, Change *change);
    // Replays a change forward (redo) or backward (undo).
    Result applyChange(const Change &change, bool undo);
    PlanPreview previewPlan(const SchemaPlan &plan);
    Result applyPlan(const SchemaPlan &plan, Change *change, QStringList *warnings = nullptr);

    [[nodiscard]] QByteArray image() const;
    Result restoreImage(const QByteArray &image);
    [[nodiscard]] quint64 revision() const { return m_revision; }

signals:
    // Rows changed in these tables (empty: any table).
    void dataChanged(const QStringList &tables);
    void schemaChanged();
    void objectsChanged();

private:
    BaseFile() = default;
    bool finishOpen(QString *error);
    Result runPlan(const SchemaPlan &plan, bool commit, QStringList *warnings);
    void announce(const QByteArray &changeset);

    Connection m_db;
    QString m_path;
    QStringList m_openProblems;
    quint64 m_revision = 0;
    mutable std::optional<CurrencyFormat> m_currency; // cache; reset when settings may change
    bool m_isQBase = false;
    bool m_readOnly = false;
};

} // namespace QindaOffice::Base
