// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M80, D-089 §4): what a form shows beyond QindaTK's
// FormDesigner — its lookup fields as combo or list boxes, its subforms
// (each a linked datasheet) and its command buttons — read, checked and
// saved as one undo step. See base_form_controls.h.
#include "base_document.h"
#include "base_form_controls.h"
#include "base_objects.h"
#include "base_records.h"
#include "base_service.h"
#include "design_files.h"

#include <QSet>

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

bool isLookup(const Engine::ColumnInfo &c)
{
    // M86: a value list is a lookup too (its own choices).
    return (c.lookup.isSet() && !c.lookup.displayField.isEmpty()) || c.lookup.isValueList();
}

QVariantList withoutNames(const QVariantList &list, const QSet<QString> &names, const QString &key)
{
    QVariantList out;
    for (const QVariant &v : list) {
        const QString name = v.metaType().id() == QMetaType::QString ? v.toString() : v.toMap().value(key).toString();
        if (!names.contains(name.toLower())) {
            out.append(v);
        }
    }
    return out;
}

} // namespace

QVariantMap designWithoutFields(const QVariantMap &design, const QStringList &fields)
{
    QSet<QString> names;
    for (const QString &f : fields) {
        names.insert(f.toLower());
    }
    QVariantMap out = design;
    QVariantMap binding = out.value(s("binding")).toMap();
    binding.insert(s("fields"), withoutNames(binding.value(s("fields")).toList(), names, s("name")));
    QVariantMap types = binding.value(s("types")).toMap();
    for (auto it = types.begin(); it != types.end();) {
        it = names.contains(it.key().toLower()) ? types.erase(it) : std::next(it);
    }
    binding.insert(s("types"), types);
    out.insert(s("binding"), binding);
    QVariantMap form = out.value(s("form")).toMap();
    form.insert(s("fields"), withoutNames(form.value(s("fields")).toList(), names, s("fieldName")));
    out.insert(s("form"), form);
    return out;
}

QVariantMap BaseService::formControls(const QString &form) const
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("form"), form)
                                                              : std::nullopt;
    if (!o) {
        return failed(tr("There is no form named %1.").arg(form));
    }
    const QString table = o->definition.value(s("source")).toMap().value(s("name")).toString();
    const QList<Engine::ColumnInfo> columns = Engine::Records::columns(*f, table);
    QVariantList lookups;
    for (const QVariant &v : o->definition.value(s("fields")).toList()) {
        const QVariantMap spec = v.toMap();
        const QString name = spec.value(s("name")).toString();
        for (const Engine::ColumnInfo &c : columns) {
            if (c.name.compare(name, Qt::CaseInsensitive) != 0 || !isLookup(c)) {
                continue;
            }
            const QString label = spec.value(s("label")).toString();
            lookups.append(QVariantMap{{s("name"), c.name},
                                       {s("label"), label.isEmpty() ? c.label() : label},
                                       {s("control"), spec.value(s("control"), s("combo")).toString()},
                                       {s("hidden"), spec.value(s("hidden")).toBool()},
                                       {s("required"), c.required},
                                       {s("related"), c.lookup.table}});
        }
    }
    return {{s("ok"), true},
            {s("table"), table},
            {s("controls"), Engine::controlsToList(Engine::formControls(o->definition))},
            {s("lookups"), lookups},
            {s("subformChoices"), Engine::controlsToList(Engine::subformChoices(*f, table))}};
}

QVariantMap BaseService::saveFormControls(const QString &form, const QVariantList &controls, const QVariantList &lookups)
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("form"), form)
                                                              : std::nullopt;
    if (!o) {
        return failed(tr("There is no form named %1.").arg(form));
    }
    const QString table = o->definition.value(s("source")).toMap().value(s("name")).toString();
    QList<Engine::FormControl> list;
    for (const QVariant &v : controls) {
        Engine::FormControl c = Engine::FormControl::fromMap(v.toMap());
        if (c.id.isEmpty()) {
            c.id = Engine::newControlId(list, c.kind);
        }
        list.append(c);
    }
    if (const QStringList problems = Engine::checkFormControls(*f, table, list); !problems.isEmpty()) {
        return failed(problems.join(QLatin1Char('\n')));
    }
    QVariantMap def = o->definition;
    def.insert(s("controls"), Engine::controlsToList(list));
    // Each lookup's control (combo or list) and whether it shows.
    QVariantList fields = def.value(s("fields")).toList();
    for (const QVariant &v : lookups) {
        const QVariantMap lookup = v.toMap();
        for (QVariant &field : fields) {
            QVariantMap spec = field.toMap();
            if (spec.value(s("name")).toString().compare(lookup.value(s("name")).toString(), Qt::CaseInsensitive) == 0) {
                spec.insert(s("control"), lookup.value(s("control"), s("combo")).toString() == QLatin1String("list")
                                              ? s("list")
                                              : s("combo"));
                spec.insert(s("hidden"), lookup.value(s("hidden")).toBool());
                field = spec;
            }
        }
    }
    def.insert(s("fields"), fields);
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("form"), o->name, def, true, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    change.label = tr("Change the controls of %1").arg(o->name);
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("controls"), Engine::controlsToList(list)}};
}

QVariantList BaseService::lookupChoices(const QString &table, const QString &field) const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    for (const Engine::ColumnInfo &c : Engine::Records::columns(*file(), table)) {
        if (c.name.compare(field, Qt::CaseInsensitive) == 0 && isLookup(c)) {
            for (const auto &[value, text] : Engine::Records::lookupChoices(*file(), c.lookup, 20000, c.type)) {
                out.append(QVariantMap{{s("value"), value}, {s("text"), text}});
            }
        }
    }
    return out;
}

DatasheetModel *BaseService::subformSheet(int index)
{
    if (index < 0 || index >= Engine::kMaxSubforms) {
        return nullptr;
    }
    while (m_subforms.size() <= index) {
        auto *sheet = new DatasheetModel(this);
        sheet->setDocument(m_document);
        connect(sheet, &DatasheetModel::messageRequested, this, &BaseService::notify);
        m_subforms.append(sheet);
    }
    return m_subforms.at(index);
}

} // namespace QindaOffice::Apps::Base
