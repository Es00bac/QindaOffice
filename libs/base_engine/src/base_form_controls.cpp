// SPDX-License-Identifier: GPL-3.0-or-later
// Form controls: subforms and command buttons kept in a form's definition,
// the subforms a table can show and the checks a form's controls pass. See
// base_form_controls.h for the contract.
#include "base_form_controls.h"

#include "base_objects.h"
#include "base_schema.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

const QStringList kKnownKeys{s("kind"), s("id"), s("caption"), s("table"), s("linkChild"), s("linkParent"),
                             s("macro"), s("actions")};

} // namespace

QVariantMap FormControl::toMap() const
{
    QVariantMap out = extra;
    out.insert(s("kind"), kind);
    out.insert(s("id"), id);
    out.insert(s("caption"), caption);
    if (kind == QLatin1String("subform")) {
        out.insert(s("table"), table);
        out.insert(s("linkChild"), linkChild);
        out.insert(s("linkParent"), linkParent);
    } else {
        out.insert(s("macro"), macro);
        Macro embedded;
        embedded.steps = actions;
        out.insert(s("actions"), embedded.toMap().value(s("actions")));
    }
    return out;
}

FormControl FormControl::fromMap(const QVariantMap &map)
{
    FormControl c;
    c.kind = map.value(s("kind")).toString();
    c.id = map.value(s("id")).toString();
    c.caption = map.value(s("caption")).toString();
    c.table = map.value(s("table")).toString();
    c.linkChild = map.value(s("linkChild")).toString();
    c.linkParent = map.value(s("linkParent")).toString();
    c.macro = map.value(s("macro")).toString();
    c.actions = Macro::fromMap({{s("actions"), map.value(s("actions"))}}).steps;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!kKnownKeys.contains(it.key())) {
            c.extra.insert(it.key(), it.value());
        }
    }
    return c;
}

QList<FormControl> formControls(const QVariantMap &definition)
{
    QList<FormControl> out;
    for (const QVariant &v : definition.value(s("controls")).toList()) {
        out.append(FormControl::fromMap(v.toMap()));
    }
    return out;
}

QVariantList controlsToList(const QList<FormControl> &controls)
{
    QVariantList out;
    for (const FormControl &c : controls) {
        out.append(c.toMap());
    }
    return out;
}

QList<FormControl> subformChoices(const BaseFile &file, const QString &table)
{
    QList<FormControl> out;
    for (const Relationship &r : readRelationships(file)) {
        if (r.parentTable.compare(table, Qt::CaseInsensitive) != 0
            || r.childTable.compare(table, Qt::CaseInsensitive) == 0) {
            continue;
        }
        FormControl c;
        c.kind = s("subform");
        c.caption = r.childTable;
        c.table = r.childTable;
        c.linkChild = r.childField;
        c.linkParent = r.parentField;
        out.append(c);
    }
    return out;
}

QStringList checkFormControls(const BaseFile &file, const QString &table, const QList<FormControl> &controls)
{
    QStringList problems;
    const std::optional<TableDef> parent = readTable(file, table);
    int subforms = 0;
    int buttons = 0;
    QStringList ids;
    for (const FormControl &c : controls) {
        const QString name = c.caption.isEmpty() ? c.id : c.caption;
        if (c.id.isEmpty() || ids.contains(c.id)) {
            problems.append(s("%1: every control needs its own id.").arg(name));
        }
        ids.append(c.id);
        if (c.kind == QLatin1String("subform")) {
            ++subforms;
            const std::optional<TableDef> child = readTable(file, c.table);
            if (!child) {
                problems.append(s("Subform %1: there is no table named %2.").arg(name, c.table));
            } else if (child->field(c.linkChild) == nullptr) {
                problems.append(s("Subform %1: %2 has no field %3.").arg(name, c.table, c.linkChild));
            }
            if (!parent || parent->field(c.linkParent) == nullptr) {
                problems.append(s("Subform %1: %2 has no field %3.").arg(name, table, c.linkParent));
            }
        } else if (c.kind == QLatin1String("button")) {
            ++buttons;
            if (c.caption.trimmed().isEmpty()) {
                problems.append(s("Every button needs a caption."));
            }
            if (!c.macro.isEmpty()) {
                if (!ObjectStore::load(file, s("macro"), c.macro)) {
                    problems.append(s("Button %1: there is no macro named %2.").arg(name, c.macro));
                }
            } else if (c.actions.isEmpty()) {
                problems.append(s("Button %1: choose what it does.").arg(name));
            } else {
                Macro embedded;
                embedded.steps = c.actions;
                for (const QString &p : checkMacro(file, embedded)) {
                    problems.append(s("Button %1: %2").arg(name, p));
                }
            }
        } else {
            problems.append(s("%1: QindaBase has no form control \"%2\".").arg(name, c.kind));
        }
    }
    if (subforms > kMaxSubforms) {
        problems.append(s("A form shows at most %1 subforms.").arg(kMaxSubforms));
    }
    if (buttons > kMaxButtons) {
        problems.append(s("A form has at most %1 buttons.").arg(kMaxButtons));
    }
    return problems;
}

QString newControlId(const QList<FormControl> &controls, const QString &kind)
{
    for (int n = 1;; ++n) {
        const QString id = kind + QString::number(n);
        if (std::none_of(controls.cbegin(), controls.cend(), [&](const FormControl &c) { return c.id == id; })) {
            return id;
        }
    }
}

} // namespace QindaOffice::Base
