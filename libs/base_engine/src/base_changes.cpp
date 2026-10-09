// SPDX-License-Identifier: GPL-3.0-or-later
// BaseFile: captured changes (the session extension), schema plans and
// whole-database images — the three things undo and redo stand on.
//
// AGENT-NOTE (D-049 §4): data undo is a session changeset recorded while
// the edit ran, so foreign-key cascades and every row they touched are in
// it; replay runs with foreign keys OFF so the captured effects are applied
// exactly once instead of cascading again. Schema undo swaps whole-database
// images (sqlite3_serialize before/after, restored through the backup API)
// because SQLite cannot invert DDL.
#define SQLITE_ENABLE_SESSION 1
#define SQLITE_ENABLE_PREUPDATE_HOOK 1
#include "base_file.h"

#include <sqlite3.h>

#include <QMap>
#include <QSet>

namespace QindaOffice::Base {

namespace {

int skipInternalTables(void *, const char *table)
{
    // Autonumbers are never handed out twice (Access semantics), so the
    // sequence table is not part of any undo step.
    const QByteArray name(table);
    return name.startsWith("sqlite_") ? 0 : 1;
}

int abortOnConflict(void *context, int conflict, sqlite3_changeset_iter *)
{
    auto *kind = static_cast<int *>(context);
    *kind = conflict;
    return SQLITE_CHANGESET_ABORT;
}

QStringList touchedTables(const QByteArray &changeset)
{
    QSet<QString> tables;
    sqlite3_changeset_iter *iter = nullptr;
    if (sqlite3changeset_start(&iter, int(changeset.size()), const_cast<char *>(changeset.constData())) != SQLITE_OK) {
        return {};
    }
    while (sqlite3changeset_next(iter) == SQLITE_ROW) {
        const char *table = nullptr;
        int columns = 0;
        int op = 0;
        int indirect = 0;
        if (sqlite3changeset_op(iter, &table, &columns, &op, &indirect) == SQLITE_OK && table != nullptr) {
            tables.insert(QString::fromUtf8(table));
        }
    }
    sqlite3changeset_finalize(iter);
    return QStringList(tables.cbegin(), tables.cend());
}

} // namespace

void BaseFile::announce(const QByteArray &changeset)
{
    ++m_revision;
    m_currency.reset();
    const QStringList tables = touchedTables(changeset);
    bool objects = false;
    QStringList user;
    for (const QString &table : tables) {
        if (table.startsWith(QLatin1String("qb_"))) {
            objects = true;
        } else {
            user.append(table);
        }
    }
    if (objects) {
        emit objectsChanged();
    }
    if (!user.isEmpty() || tables.isEmpty()) {
        emit dataChanged(user);
    }
}

Result BaseFile::capture(const QString &label, const std::function<Result()> &body, Change *change)
{
    if (m_readOnly) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    sqlite3_session *session = nullptr;
    if (sqlite3session_create(m_db.handle(), "main", &session) != SQLITE_OK) {
        return Result::failure(m_db.lastError());
    }
    int rowid = 1;
    sqlite3session_object_config(session, SQLITE_SESSION_OBJCONFIG_ROWID, &rowid);
    sqlite3session_table_filter(session, skipInternalTables, nullptr);
    sqlite3session_attach(session, nullptr);

    Result result;
    {
        Savepoint sp(m_db, QStringLiteral("qb_edit"));
        if (!sp.isActive()) {
            sqlite3session_delete(session);
            return Result::failure(sp.error());
        }
        result = body();
        if (result.ok) {
            result = sp.release();
        }
    }
    QByteArray bytes;
    if (result.ok) {
        int size = 0;
        void *data = nullptr;
        if (sqlite3session_changeset(session, &size, &data) == SQLITE_OK && data != nullptr) {
            bytes = QByteArray(static_cast<const char *>(data), size);
        }
        sqlite3_free(data);
    }
    sqlite3session_delete(session);
    if (!result.ok) {
        return result;
    }
    if (change != nullptr) {
        change->kind = Change::Kind::Data;
        change->label = label;
        change->changeset = bytes;
    }
    if (!bytes.isEmpty()) {
        announce(bytes);
    }
    return Result::success();
}

Result BaseFile::applyChange(const Change &change, bool undo)
{
    if (m_readOnly) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    if (change.kind == Change::Kind::Schema) {
        const QByteArray &target = undo ? change.before : change.after;
        if (target.isEmpty()) {
            return Result::failure(QStringLiteral("This schema change cannot be undone (the database is too large "
                                                  "to keep a copy in memory); its backup is %1.")
                                       .arg(change.backupPath));
        }
        const Result restored = restoreImage(target);
        if (restored.ok) {
            ++m_revision;
            emit schemaChanged();
            emit objectsChanged();
            emit dataChanged({});
        }
        return restored;
    }
    if (change.changeset.isEmpty()) {
        return Result::success();
    }
    QByteArray bytes = change.changeset;
    if (undo) {
        int size = 0;
        void *inverted = nullptr;
        if (sqlite3changeset_invert(int(bytes.size()), bytes.data(), &size, &inverted) != SQLITE_OK) {
            return Result::failure(QStringLiteral("The change could not be inverted."));
        }
        bytes = QByteArray(static_cast<const char *>(inverted), size);
        sqlite3_free(inverted);
    }
    m_db.exec(QStringLiteral("PRAGMA foreign_keys=OFF"));
    int conflict = 0;
    const int rc = sqlite3changeset_apply(m_db.handle(), int(bytes.size()), bytes.data(), nullptr, abortOnConflict,
                                          &conflict);
    m_db.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (rc != SQLITE_OK) {
        return Result::failure(conflict != 0 ? QStringLiteral("The records changed since \"%1\"; it can no longer be %2.")
                                                   .arg(change.label, undo ? QStringLiteral("undone")
                                                                           : QStringLiteral("redone"))
                                             : m_db.lastError());
    }
    announce(bytes);
    return Result::success();
}

QByteArray BaseFile::image() const
{
    sqlite3_int64 size = 0;
    unsigned char *data = sqlite3_serialize(m_db.handle(), "main", &size, 0);
    if (data == nullptr) {
        return {};
    }
    QByteArray out(reinterpret_cast<const char *>(data), qsizetype(size));
    sqlite3_free(data);
    return out;
}

Result BaseFile::restoreImage(const QByteArray &bytes)
{
    if (m_db.inTransaction()) {
        return Result::failure(QStringLiteral("A change is still in progress."));
    }
    sqlite3 *scratch = nullptr;
    if (sqlite3_open(":memory:", &scratch) != SQLITE_OK) {
        sqlite3_close(scratch);
        return Result::failure(QStringLiteral("Out of memory."));
    }
    // AGENT-GUARD: READONLY without FREEONCLOSE borrows `bytes`, which
    // outlives `scratch` (closed below before returning).
    auto *data = reinterpret_cast<unsigned char *>(const_cast<char *>(bytes.constData()));
    if (sqlite3_deserialize(scratch, "main", data, bytes.size(), bytes.size(), SQLITE_DESERIALIZE_READONLY)
        != SQLITE_OK) {
        sqlite3_close(scratch);
        return Result::failure(QStringLiteral("The saved copy is damaged."));
    }
    sqlite3_backup *backup = sqlite3_backup_init(m_db.handle(), "main", scratch, "main");
    if (backup == nullptr) {
        const QString error = m_db.lastError();
        sqlite3_close(scratch);
        return Result::failure(error);
    }
    const int step = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(scratch);
    if (step != SQLITE_DONE) {
        return Result::failure(m_db.lastError());
    }
    m_db.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    m_currency.reset();
    return Result::success();
}

Result BaseFile::runPlan(const SchemaPlan &plan, bool commit, QStringList *warnings)
{
    if (!plan.ok()) {
        return Result::failure(plan.error);
    }
    if (m_db.inTransaction()) {
        return Result::failure(QStringLiteral("A change is still in progress."));
    }
    // Foreign keys must be switched off OUTSIDE the transaction for table
    // rebuilds (sqlite.org/lang_altertable.html §7); the check runs before
    // commit instead.
    m_db.exec(QStringLiteral("PRAGMA foreign_keys=OFF"));
    if (plan.legacyAlter) {
        m_db.exec(QStringLiteral("PRAGMA legacy_alter_table=ON"));
    }
    const auto restorePragmas = [this, &plan] {
        if (plan.legacyAlter) {
            m_db.exec(QStringLiteral("PRAGMA legacy_alter_table=OFF"));
        }
        m_db.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    };
    Result begun = m_db.exec(QStringLiteral("BEGIN IMMEDIATE"));
    if (!begun.ok) {
        restorePragmas();
        return begun;
    }
    const auto abort = [&](const QString &error) {
        m_db.exec(QStringLiteral("ROLLBACK"));
        restorePragmas();
        return Result::failure(error);
    };
    for (int i = 0; i < plan.statements.size(); ++i) {
        const Result r = m_db.exec(plan.statements.at(i));
        if (!r.ok) {
            return abort(plan.statements.size() > 1 ? QStringLiteral("Step %1 failed: %2").arg(i + 1).arg(r.error)
                                                    : r.error);
        }
    }
    const QList<QVariantList> orphans = m_db.rows(QStringLiteral("PRAGMA foreign_key_check"));
    if (!orphans.isEmpty()) {
        QMap<QString, int> byTable;
        for (const QVariantList &row : orphans) {
            byTable[QStringLiteral("%1 → %2").arg(row.value(0).toString(), row.value(2).toString())] += 1;
        }
        QStringList lines;
        for (auto it = byTable.cbegin(); it != byTable.cend(); ++it) {
            lines.append(QStringLiteral("%1 record(s) in %2 have no matching record").arg(it.value()).arg(it.key()));
        }
        return abort(QStringLiteral("The relationship cannot be enforced: ") + lines.join(QStringLiteral("; ")));
    }
    for (const auto &check : plan.checks) {
        const qint64 count = m_db.scalar(check.first).toLongLong();
        if (count > 0 && warnings != nullptr) {
            warnings->append(check.second.arg(count));
        }
    }
    for (const auto &refusal : plan.refusals) {
        QString problem;
        const qint64 count = m_db.scalar(refusal.first, {}, &problem).toLongLong();
        if (!problem.isEmpty() || count > 0) {
            return abort(problem.isEmpty() ? refusal.second.arg(count) : problem);
        }
    }
    if (!commit) {
        m_db.exec(QStringLiteral("ROLLBACK"));
        restorePragmas();
        return Result::success();
    }
    const Result committed = m_db.exec(QStringLiteral("COMMIT"));
    if (!committed.ok) {
        return abort(committed.error);
    }
    restorePragmas();
    return Result::success();
}

PlanPreview BaseFile::previewPlan(const SchemaPlan &plan)
{
    PlanPreview preview;
    if (m_readOnly) {
        preview.error = QStringLiteral("The database is read-only.");
        return preview;
    }
    const Result r = runPlan(plan, false, &preview.warnings);
    preview.ok = r.ok;
    preview.error = r.error;
    return preview;
}

Result BaseFile::applyPlan(const SchemaPlan &plan, Change *change, QStringList *warnings)
{
    if (m_readOnly) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    Change made;
    made.kind = Change::Kind::Schema;
    made.label = plan.label;
    const qint64 size = m_db.scalar(QStringLiteral("SELECT page_count * page_size FROM pragma_page_count(), "
                                                   "pragma_page_size()"))
                            .toLongLong();
    const bool keepImages = size <= kUndoImageLimit;
    if (plan.destructive || !keepImages) {
        QString error;
        made.backupPath = automaticBackup(plan.label, &error);
        if (made.backupPath.isEmpty()) {
            return Result::failure(QStringLiteral("No backup could be made, so nothing was changed: ") + error);
        }
    }
    if (keepImages) {
        made.before = image();
    }
    const Result r = runPlan(plan, true, warnings);
    if (!r.ok) {
        return r;
    }
    if (keepImages) {
        made.after = image();
    }
    ++m_revision;
    if (change != nullptr) {
        *change = made;
    }
    emit schemaChanged();
    emit objectsChanged();
    emit dataChanged({});
    return Result::success();
}

} // namespace QindaOffice::Base
