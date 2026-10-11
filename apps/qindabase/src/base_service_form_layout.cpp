// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M89, D-097): forms on QindaTK's positioned controls
// (FormDesigner, QindaTK D-311). The design a form opens with — its saved
// design brought to the table as it is now, or, for a form saved before
// M89, its fields and M80's controls laid out once (base_form_layout.h) —
// and what the window needs around it: a lookup box's items and keys, a
// calculated control's value, Add Subform's choices and the table a
// subform's form name shows.
#include "base_form_controls.h"
#include "base_form_layout.h"
#include "base_objects.h"
#include "base_records.h"
#include "base_service.h"
#include "design_files.h"

#include <qindatk/local_data_design_document.h>
#include <qindatk/local_database_provider.h>

#include <QHash>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

const Engine::ColumnInfo *columnNamed(const QList<Engine::ColumnInfo> &columns, const QString &name)
{
    for (const Engine::ColumnInfo &c : columns) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &c;
        }
    }
    return nullptr;
}

// The form's own field entries ({name, label, hidden}) by field name.
QHash<QString, QVariantMap> fieldSpecs(const QVariantMap &definition)
{
    QHash<QString, QVariantMap> out;
    for (const QVariant &v : definition.value(s("fields")).toList()) {
        out.insert(v.toMap().value(s("name")).toString().toLower(), v.toMap());
    }
    return out;
}

} // namespace

QVariantMap formDesignFor(const Engine::BaseFile &file, const QVariantMap &definition, const QString &table,
                          QindaTK::LocalDatabaseProvider *mirror, QString *notice, QString *error)
{
    QindaTK::LocalDataDesignDocument doc;
    QVariantMap stored = definition.value(s("design")).toMap();
    stored.remove(s("session"));
    bool fromStored = false;
    QStringList storedFields;
    if (!stored.isEmpty()) {
        fromStored = doc.setDesign(stored);
        if (fromStored) {
            for (const QVariant &v : doc.form().value(s("fields")).toList()) {
                storedFields.append(v.toMap().value(s("fieldName")).toString());
            }
        } else {
            *notice = BaseService::tr("The form's layout no longer matched the table (%1); it was laid out again.")
                          .arg(doc.lastError());
        }
    }
    // Every mirror column is bound, so any control can show any field;
    // fields the design already lists keep their labels and order, and a
    // control whose field is gone keeps its place, unbound (bindTable).
    QStringList names;
    for (const QVariant &v : mirror->schema(table).value(s("fields")).toList()) {
        names.append(v.toMap().value(s("name")).toString());
    }
    if (!doc.bindTable(mirror, table, names)) {
        *error = doc.lastError();
        return {};
    }
    // Labels and shown flags for the fields the design did not list yet:
    // the form's own entries (M49/M80), else the column's caption, hidden.
    const QHash<QString, QVariantMap> specs = fieldSpecs(definition);
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(file, table);
    for (const QString &name : std::as_const(names)) {
        if (storedFields.contains(name)) {
            continue;
        }
        const QVariantMap spec = specs.value(name.toLower());
        const Engine::ColumnInfo *c = columnNamed(columns, name);
        const QString label = spec.value(s("label")).toString();
        static_cast<void>(doc.setFormFieldLabel(name, !label.isEmpty() ? label : c != nullptr ? c->label() : name));
        static_cast<void>(doc.setFormFieldHidden(name, spec.isEmpty() || spec.value(s("hidden")).toBool()));
    }
    if (!fromStored) {
        // A form without a saved design shows its fields in its own order.
        int index = 0;
        for (const QVariant &v : definition.value(s("fields")).toList()) {
            const QString name = v.toMap().value(s("name")).toString();
            if (names.contains(name) && doc.moveFormField(name, index)) {
                ++index;
            }
        }
        static_cast<void>(doc.setFormTitle(definition.value(s("title"), table).toString()));
    }
    // Positioned controls: a form saved before M89 is laid out once.
    const bool laidOut = definition.value(s("positioned")).toBool() || doc.form().contains(s("controls"));
    if (!laidOut && !doc.setFormControls(Engine::layOutForm(file, table, definition))) {
        *notice = BaseService::tr("The form's controls could not be laid out (%1).").arg(doc.lastError());
    }
    QVariantMap design = doc.design();
    design.remove(s("session"));
    return design;
}

QVariantMap BaseService::lookupItems(const QString &table, const QString &field) const
{
    if (file() == nullptr) {
        return {{s("ok"), false}, {s("error"), tr("No database is open.")}};
    }
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*file(), table);
    const Engine::ColumnInfo *c = columnNamed(columns, field);
    if (c == nullptr || !Engine::isLookupColumn(*c)) {
        return {{s("ok"), true}, {s("lookup"), false}};
    }
    const Engine::LookupItems items = Engine::lookupItems(*file(), *c);
    return {{s("ok"), true},
            {s("lookup"), true},
            {s("required"), c->required},
            {s("items"), items.labels},
            {s("keys"), items.keys}};
}

QVariantMap BaseService::formLookups(const QString &table) const
{
    QVariantMap out;
    if (file() == nullptr) {
        return out;
    }
    for (const Engine::ColumnInfo &c : Engine::Records::columns(*file(), table)) {
        if (!Engine::isLookupColumn(c)) {
            continue;
        }
        const Engine::LookupItems items = Engine::lookupItems(*file(), c);
        out.insert(c.name, QVariantMap{{s("items"), items.labels}, {s("keys"), items.keys}, {s("required"), c.required}});
    }
    return out;
}

QVariantMap BaseService::evaluateFormExpression(const QString &table, const QString &expression,
                                                const QVariantMap &record) const
{
    if (file() == nullptr) {
        return {{s("ok"), false}, {s("error"), tr("No database is open.")}};
    }
    const Engine::ControlValue v = Engine::evaluateControlExpression(*file(), table, expression, record);
    return {{s("ok"), v.ok}, {s("text"), v.text}, {s("error"), v.error}};
}

QVariantList BaseService::subformChoices(const QString &form) const
{
    QVariantList out;
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("form"), form)
                                                              : std::nullopt;
    if (!o) {
        return out;
    }
    const QString table = o->definition.value(s("source")).toMap().value(s("name")).toString();
    for (const Engine::FormControl &c : Engine::subformChoices(*f, table)) {
        const QString shown = Engine::formOverTable(*f, c.table);
        out.append(QVariantMap{{s("caption"), c.caption},
                               {s("table"), c.table},
                               {s("formName"), shown.isEmpty() ? c.table : shown},
                               {s("linkChild"), c.linkChild},
                               {s("linkParent"), c.linkParent}});
    }
    return out;
}

QVariantMap BaseService::subformTable(const QString &formName) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return {{s("ok"), false}, {s("error"), tr("No database is open.")}};
    }
    if (const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, s("form"), formName)) {
        return {{s("ok"), true}, {s("table"), o->definition.value(s("source")).toMap().value(s("name")).toString()}};
    }
    if (!formName.trimmed().isEmpty() && f->tableExists(formName)) {
        return {{s("ok"), true}, {s("table"), formName}};
    }
    return {{s("ok"), false},
            {s("error"), formName.trimmed().isEmpty() ? tr("Give the subform a form to show (Properties ▸ Form).")
                                                      : tr("There is no form or table named %1.").arg(formName)}};
}

} // namespace QindaOffice::Apps::Base
