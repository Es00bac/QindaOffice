// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: forms (a mirror schema + a QindaTK design file + record
// navigation and saves) and reports (a snapshot of the source + a design
// file). See base_snapshot.h for why the toolkit gets scratch databases.
#include "base_document.h"
#include "base_objects.h"
#include "base_records.h"
#include "base_service.h"
#include "base_snapshot.h"
#include "design_files.h"

#include <qindatk/local_data_design_document.h>
#include <qindatk/local_data_design_store.h>
#include <qindatk/local_database_provider.h>

#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

} // namespace

QVariantMap BaseService::createForm(const QString &name, const QString &table, const QString &title,
                                    const QStringList &fields)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    if (!f->tableExists(table)) {
        return failed(tr("There is no table named %1.").arg(table));
    }
    QVariantList list;
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*f, table);
    for (const Engine::ColumnInfo &c : columns) {
        const bool wanted = fields.isEmpty() ? c.type != Engine::FieldType::AutoNumber : fields.contains(c.name);
        if (wanted) {
            list.append(QVariantMap{{s("name"), c.name}, {s("label"), c.label()}, {s("hidden"), false}});
        }
    }
    const QString objectName = name.trimmed().isEmpty() ? Engine::ObjectStore::uniqueName(*f, s("form"), table)
                                                        : name.trimmed();
    const QVariantMap def{{s("version"), 1},
                          {s("source"), QVariantMap{{s("kind"), s("table")}, {s("name"), table}}},
                          {s("title"), title.isEmpty() ? table : title},
                          {s("fields"), list}};
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("form"), objectName, def, false, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("name"), objectName}};
}

QVariantMap BaseService::openForm(const QString &name)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, s("form"), name);
    if (!o) {
        return failed(tr("There is no form named %1.").arg(name));
    }
    const QString table = o->definition.value(s("source")).toMap().value(s("name")).toString();
    if (!f->tableExists(table)) {
        return failed(tr("The form %1 shows the table %2, which no longer exists.").arg(name, table));
    }
    const QString mirror = scratchPath(s("forms.sqlite"));
    const Engine::Result mirrored = Engine::writeFormMirror(*f, mirror, table);
    if (!mirrored.ok) {
        return failed(mirrored.error);
    }
    QindaTK::LocalDatabaseProvider provider;
    provider.open(mirror);
    static int serial = 0;
    const QString designPath = scratchPath(QStringLiteral("form-%1.qtkdata").arg(++serial));
    QString error;
    QString notice;
    const QVariantMap saved = o->definition.value(s("design")).toMap();
    if (saved.isEmpty() || !writeDesignFile(designPath, saved, &provider, &error)) {
        if (!saved.isEmpty()) {
            notice = tr("The form's layout no longer matched the table (%1); it was rebuilt.").arg(error);
        }
        // Build the toolkit design from the form's field list.
        QindaTK::LocalDataDesignDocument doc;
        QStringList names;
        const QVariantList specFields = o->definition.value(s("fields")).toList();
        for (const QVariant &v : specFields) {
            names.append(v.toMap().value(s("name")).toString());
        }
        if (!doc.bindTable(&provider, table, names)) {
            return failed(doc.lastError());
        }
        doc.setFormTitle(o->definition.value(s("title"), table).toString());
        const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*f, table);
        for (const QVariant &v : specFields) {
            const QVariantMap m = v.toMap();
            const QString field = m.value(s("name")).toString();
            QString label = m.value(s("label")).toString();
            for (const Engine::ColumnInfo &c : columns) {
                if (label.isEmpty() && c.name == field) {
                    label = c.label();
                }
            }
            doc.setFormFieldLabel(field, label.isEmpty() ? field : label);
            if (m.value(s("hidden")).toBool()) {
                doc.setFormFieldHidden(field, true);
            }
        }
        if (!writeDesignFile(designPath, doc.design(), &provider, &error)) {
            return failed(error);
        }
    }
    return {{s("ok"), true},
            {s("table"), table},
            {s("title"), o->definition.value(s("title"), table)},
            {s("mirrorPath"), mirror},
            {s("designPath"), designPath},
            {s("notice"), notice}};
}

QVariantMap BaseService::saveFormDesign(const QString &name, const QVariantMap &design)
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o =
        f != nullptr ? Engine::ObjectStore::load(*f, s("form"), name) : std::nullopt;
    if (!o) {
        return failed(tr("There is no form named %1.").arg(name));
    }
    QVariantMap def = o->definition;
    const QVariantMap form = design.value(s("form")).toMap();
    QVariantList fields;
    for (const QVariant &v : form.value(s("fields")).toList()) {
        const QVariantMap m = v.toMap();
        fields.append(QVariantMap{{s("name"), m.value(s("fieldName"))}, {s("label"), m.value(s("label"))},
                                  {s("hidden"), m.value(s("hidden"), false)}});
    }
    def.insert(s("design"), design);
    def.insert(s("title"), form.value(s("title")));
    def.insert(s("fields"), fields);
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("form"), name, def, true, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}};
}

QVariantList BaseService::formRowIds(const QString &table) const
{
    QVariantList out;
    if (file() == nullptr || !file()->tableExists(table)) {
        return out;
    }
    for (const QVariantList &row : file()->db().rows(
             QStringLiteral("SELECT rowid FROM %1 ORDER BY rowid LIMIT 100000").arg(Engine::quoteIdentifier(table)))) {
        out.append(row.value(0));
    }
    return out;
}

QVariantMap BaseService::formRecord(const QString &table, qint64 rowid) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*f, table);
    QVariantMap record;
    if (rowid > 0) {
        const std::optional<QVariantList> values = Engine::Records::readRow(*f, table, rowid, columns);
        if (!values) {
            return failed(tr("That record no longer exists."));
        }
        const Engine::CurrencyFormat currency = f->currency();
        for (int i = 0; i < columns.size(); ++i) {
            const Engine::ColumnInfo &c = columns.at(i);
            const QVariant v = values->at(i);
            QVariant shown = v;
            if (!v.isValid()) {
                shown = QVariant();
            } else if (c.lookup.isSet() && !c.lookup.displayField.isEmpty()) {
                shown = v.toString();
                for (const auto &[value, text] : Engine::Records::lookupChoices(*f, c.lookup)) {
                    if (value.toString() == v.toString()) {
                        shown = text;
                    }
                }
            } else if (c.type == Engine::FieldType::YesNo) {
                shown = v.toLongLong() != 0;
            } else if (c.type == Engine::FieldType::Currency) {
                shown = Engine::editText(c.type, v, currency);
            }
            record.insert(c.name, shown);
        }
    }
    return {{s("ok"), true}, {s("recordId"), rowid > 0 ? QString::number(rowid) : QString()}, {s("record"), record}};
}

QVariantMap BaseService::saveFormRecord(const QString &table, qint64 rowid, const QVariantMap &changes)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*f, table);
    QVariantMap values;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const auto column = std::find_if(columns.cbegin(), columns.cend(),
                                         [&](const Engine::ColumnInfo &c) { return c.name == it.key(); });
        if (column != columns.cend() && !column->readOnly) {
            values.insert(it.key(), it.value());
        }
    }
    Engine::Change change;
    Engine::Result r;
    qint64 id = rowid;
    if (rowid <= 0) {
        r = Engine::Records::insertRecord(*f, table, values, &id, &change);
    } else {
        QList<Engine::CellEdit> edits;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            edits.append({rowid, it.key(), it.value()});
        }
        r = Engine::Records::updateCells(*f, table, edits, &change);
    }
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("rowid"), id}};
}

QVariantMap BaseService::deleteFormRecord(const QString &table, qint64 rowid)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    Engine::Change change;
    const Engine::Result r = Engine::Records::deleteRecords(*f, table, {rowid}, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}};
}

bool writeDesignFile(const QString &path, const QVariantMap &design, QindaTK::LocalDatabaseProvider *provider,
                     QString *error)
{
    QindaTK::LocalDataDesignStore store;
    const QVariantMap checked = store.validate(design, provider);
    if (!checked.value(s("ok")).toBool()) {
        *error = checked.value(s("error")).toString();
        return false;
    }
    QVariantMap body = design;
    body.remove(s("session"));
    body.insert(s("version"), 2);
    const QindaTK::SnapshotHeader header{QindaTK::LocalDataDesignDocument::schemaId(), QindaTK::DocumentId::create(),
                                         QindaTK::RevisionId{1}, 0};
    const QByteArray bytes =
        QindaTK::LocalDataDesignDocument::encodeState(QJsonObject::fromVariantMap(body), header, QJsonObject());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace QindaOffice::Apps::Base
