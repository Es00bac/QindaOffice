// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_objects.h"

#include "base_schema.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Base {

namespace {

QString now()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString toJson(const QVariantMap &map)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

QVariantMap fromJson(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object().toVariantMap();
}

SavedObject fromRow(const QVariantList &row)
{
    SavedObject o;
    o.id = row.value(0).toLongLong();
    o.kind = row.value(1).toString();
    o.name = row.value(2).toString();
    o.definition = fromJson(row.value(3).toString());
    o.description = row.value(4).toString();
    o.created = row.value(5).toString();
    o.modified = row.value(6).toString();
    return o;
}

const QString kSelect = QStringLiteral("SELECT id, kind, name, definition, description, created, modified FROM qb_objects");

// Renames table references inside one definition; true when it changed.
bool renameIn(QVariantMap &def, const QString &kind, const QString &from, const QString &to)
{
    bool changed = false;
    const auto same = [&](const QVariant &v) { return v.toString().compare(from, Qt::CaseInsensitive) == 0; };
    if (kind == QLatin1String("query")) {
        QVariantList tables = def.value(QStringLiteral("tables")).toList();
        for (QVariant &t : tables) {
            QVariantMap m = t.toMap();
            if (same(m.value(QStringLiteral("name")))) {
                const bool aliased = !m.value(QStringLiteral("alias")).toString().isEmpty();
                m.insert(QStringLiteral("name"), to);
                t = m;
                changed = true;
                if (!aliased) {
                    // Unaliased references use the table name as their ref.
                    for (const QString key : {QStringLiteral("joins"), QStringLiteral("columns")}) {
                        QVariantList items = def.value(key).toList();
                        for (QVariant &item : items) {
                            QVariantMap im = item.toMap();
                            for (const QString field : {QStringLiteral("leftTable"), QStringLiteral("rightTable"),
                                                        QStringLiteral("table")}) {
                                if (same(im.value(field))) {
                                    im.insert(field, to);
                                }
                            }
                            item = im;
                        }
                        def.insert(key, items);
                    }
                }
            }
        }
        def.insert(QStringLiteral("tables"), tables);
        if (same(def.value(QStringLiteral("appendTable")))) {
            def.insert(QStringLiteral("appendTable"), to);
            changed = true;
        }
        return changed;
    }
    QVariantMap source = def.value(QStringLiteral("source")).toMap();
    if (source.value(QStringLiteral("kind")).toString() == QLatin1String("table") && same(source.value(QStringLiteral("name")))) {
        source.insert(QStringLiteral("name"), to);
        def.insert(QStringLiteral("source"), source);
        def.remove(QStringLiteral("design")); // rebuilt from the spec against the renamed table
        changed = true;
    }
    return changed;
}

} // namespace

QVariantMap SavedObject::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},
            {QStringLiteral("definition"), definition},
            {QStringLiteral("description"), description},
            {QStringLiteral("created"), created},
            {QStringLiteral("modified"), modified}};
}

bool ObjectStore::validKind(const QString &kind)
{
    return kind == QLatin1String("query") || kind == QLatin1String("form") || kind == QLatin1String("report");
}

QList<SavedObject> ObjectStore::list(const BaseFile &file, const QString &kind)
{
    QList<SavedObject> out;
    if (!file.hasSystemTables()) {
        return out;
    }
    const QList<QVariantList> rows =
        kind.isEmpty() ? file.db().rows(kSelect + QStringLiteral(" ORDER BY kind, name COLLATE NOCASE"))
                       : file.db().rows(kSelect + QStringLiteral(" WHERE kind=? ORDER BY name COLLATE NOCASE"), {kind});
    for (const QVariantList &row : rows) {
        out.append(fromRow(row));
    }
    return out;
}

std::optional<SavedObject> ObjectStore::load(const BaseFile &file, const QString &kind, const QString &name)
{
    if (!file.hasSystemTables()) {
        return std::nullopt;
    }
    const QList<QVariantList> rows =
        file.db().rows(kSelect + QStringLiteral(" WHERE kind=? AND name=? COLLATE NOCASE"), {kind, name});
    if (rows.isEmpty()) {
        return std::nullopt;
    }
    return fromRow(rows.first());
}

Result ObjectStore::save(BaseFile &file, const QString &kind, const QString &name, const QVariantMap &definition,
                         bool replace, Change *change)
{
    if (!validKind(kind)) {
        return Result::failure(QStringLiteral("Unknown object kind %1.").arg(kind));
    }
    if (QString e = validateName(name, kind); !e.isEmpty()) {
        return Result::failure(e);
    }
    if (!file.hasSystemTables()) {
        return Result::failure(QStringLiteral("This database has no QindaBase system tables yet."));
    }
    const std::optional<SavedObject> existing = load(file, kind, name);
    if (existing && !replace) {
        return Result::failure(QStringLiteral("There is already a %1 named %2.").arg(kind, name));
    }
    const QString json = toJson(definition);
    const QString description = definition.value(QStringLiteral("description")).toString();
    return file.capture(QStringLiteral("Save %1 %2").arg(kind, name), [&]() -> Result {
        if (existing) {
            return file.db().run(QStringLiteral("UPDATE qb_objects SET name=?, definition=?, description=?, modified=? "
                                                "WHERE id=?"),
                                 {name, json, description, now(), existing->id});
        }
        return file.db().run(QStringLiteral("INSERT INTO qb_objects(kind, name, definition, description, created, "
                                            "modified) VALUES(?, ?, ?, ?, ?, ?)"),
                             {kind, name, json, description, now(), now()});
    }, change);
}

Result ObjectStore::rename(BaseFile &file, const QString &kind, const QString &from, const QString &to, Change *change)
{
    if (QString e = validateName(to, kind); !e.isEmpty()) {
        return Result::failure(e);
    }
    const std::optional<SavedObject> existing = load(file, kind, from);
    if (!existing) {
        return Result::failure(QStringLiteral("There is no %1 named %2.").arg(kind, from));
    }
    if (from.compare(to, Qt::CaseInsensitive) != 0 && load(file, kind, to)) {
        return Result::failure(QStringLiteral("There is already a %1 named %2.").arg(kind, to));
    }
    return file.capture(QStringLiteral("Rename %1 to %2").arg(from, to), [&]() -> Result {
        return file.db().run(QStringLiteral("UPDATE qb_objects SET name=?, modified=? WHERE id=?"),
                             {to, now(), existing->id});
    }, change);
}

Result ObjectStore::remove(BaseFile &file, const QString &kind, const QString &name, Change *change)
{
    const std::optional<SavedObject> existing = load(file, kind, name);
    if (!existing) {
        return Result::failure(QStringLiteral("There is no %1 named %2.").arg(kind, name));
    }
    return file.capture(QStringLiteral("Delete %1 %2").arg(kind, name), [&]() -> Result {
        return file.db().run(QStringLiteral("DELETE FROM qb_objects WHERE id=?"), {existing->id});
    }, change);
}

QString ObjectStore::uniqueName(const BaseFile &file, const QString &kind, const QString &base)
{
    QString candidate = base;
    for (int n = 2; load(file, kind, candidate); ++n) {
        candidate = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    return candidate;
}

QStringList ObjectStore::referencesTo(const BaseFile &file, const QString &table)
{
    QStringList out;
    for (const SavedObject &o : list(file)) {
        QVariantMap copy = o.definition;
        const bool named = renameIn(copy, o.kind, table, QStringLiteral("\x01"));
        const bool inSql = o.kind == QLatin1String("query")
                           && o.definition.value(QStringLiteral("sql")).toString().contains(table, Qt::CaseInsensitive);
        if (named || inSql) {
            out.append(QStringLiteral("%1 %2").arg(o.kind, o.name));
        }
    }
    return out;
}

QStringList ObjectStore::tableRenameStatements(const BaseFile &file, const QString &from, const QString &to)
{
    QStringList out;
    for (const SavedObject &o : list(file)) {
        QVariantMap def = o.definition;
        if (renameIn(def, o.kind, from, to)) {
            out.append(QStringLiteral("UPDATE qb_objects SET definition = %1 WHERE id = %2")
                           .arg(quoteLiteral(toJson(def)))
                           .arg(o.id));
        }
    }
    return out;
}

} // namespace QindaOffice::Base
