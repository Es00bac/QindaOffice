// SPDX-License-Identifier: GPL-3.0-or-later
// A form's positioned controls (M89, D-097): the stacked layout of a form
// saved before M89, lookup labels and keys, calculated controls. See
// base_form_layout.h for the contract.
#include "base_form_layout.h"

#include "base_expression.h"
#include "base_form_controls.h"
#include "base_macro.h"
#include "base_objects.h"
#include "base_schema.h"
#include "base_sqlite.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Base {

namespace {

using U = FormLayoutUnits;

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

// Ids are text of at most 64 characters (QindaTK checkFormControls), unique.
class Ids {
public:
    QString take(const QString &wanted, const QString &fallback)
    {
        QString base = wanted.size() <= 56 && !wanted.trimmed().isEmpty() ? wanted : fallback;
        QString id = base;
        for (int n = 2; m_taken.contains(id); ++n) {
            id = base + QString::number(n);
        }
        m_taken.insert(id);
        return id;
    }

private:
    QSet<QString> m_taken;
};

QVariantMap control(const QString &id, const char *type, const QString &name, int x, int y, int width, int height,
                    const QVariantMap &properties = {})
{
    QVariantMap out = properties;
    out.insert(s("id"), id);
    out.insert(s("type"), s(type));
    out.insert(s("name"), name);
    out.insert(s("x"), x);
    out.insert(s("y"), y);
    out.insert(s("width"), width);
    out.insert(s("height"), height);
    return out;
}

const ColumnInfo *columnNamed(const QList<ColumnInfo> &columns, const QString &name)
{
    for (const ColumnInfo &c : columns) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &c;
        }
    }
    return nullptr;
}

// What an M80 button ran, as a command button's action id and arguments.
QVariantMap buttonAction(const FormControl &button)
{
    if (!button.macro.isEmpty()) {
        return {{s("actionId"), s("runMacro")}, {s("arguments"), QVariantMap{{s("macro"), button.macro}}}};
    }
    if (button.actions.isEmpty()) {
        return {{s("actionId"), QString()}};
    }
    QVariantMap out{{s("actionId"), button.actions.first().action}, {s("arguments"), button.actions.first().arguments}};
    if (button.actions.size() > 1) {
        Macro embedded;
        embedded.steps = button.actions;
        out.insert(s("actions"), embedded.toMap().value(s("actions")));
    }
    return out;
}

} // namespace

bool isLookupColumn(const ColumnInfo &column)
{
    return (column.lookup.isSet() && !column.lookup.displayField.isEmpty()) || column.lookup.isValueList();
}

QString formOverTable(const BaseFile &file, const QString &table)
{
    for (const SavedObject &o : ObjectStore::list(file, s("form"))) {
        const QVariantMap source = o.definition.value(s("source")).toMap();
        if (source.value(s("name")).toString().compare(table, Qt::CaseInsensitive) == 0) {
            return o.name;
        }
    }
    return {};
}

QVariantList layOutForm(const BaseFile &file, const QString &table, const QVariantMap &definition)
{
    const QList<ColumnInfo> columns = Records::columns(file, table);
    QVariantList specs = definition.value(s("fields")).toList();
    if (specs.isEmpty()) {
        for (const ColumnInfo &c : columns) {
            if (isTypable(c.type) && c.type != FieldType::AutoNumber) {
                specs.append(QVariantMap{{s("name"), c.name}});
            }
        }
    }
    Ids ids;
    for (const FormControl &c : formControls(definition)) {
        static_cast<void>(ids.take(c.id, c.kind)); // M80's ids stay theirs
    }
    QVariantList out;
    int y = U::gap;
    int n = 0;
    for (const QVariant &v : std::as_const(specs)) {
        const QVariantMap spec = v.toMap();
        const ColumnInfo *c = columnNamed(columns, spec.value(s("name")).toString());
        if (spec.value(s("hidden")).toBool() || c == nullptr || c->type == FieldType::File) {
            continue; // file fields show beside the form (FileFieldPanel.qml)
        }
        ++n;
        const QString label = spec.value(s("label")).toString().isEmpty() ? c->label() : spec.value(s("label")).toString();
        const QString id = ids.take(c->name, s("field%1").arg(n));
        if (c->type == FieldType::YesNo) {
            out.append(control(id, "checkBox", c->name, U::controlLeft, y, U::controlWidth, U::lineHeight,
                               {{s("fieldName"), c->name}, {s("caption"), label}}));
            y += U::lineHeight + U::gap;
            continue;
        }
        out.append(control(ids.take(id + s("_label"), s("label%1").arg(n)), "label", c->name + s("_Label"), U::left, y,
                           U::labelWidth, U::lineHeight, {{s("caption"), label}}));
        if (isLookupColumn(*c)) {
            const bool list = spec.value(s("control")).toString() == QLatin1String("list");
            const int height = list ? U::listHeight : U::lineHeight;
            out.append(control(id, list ? "listBox" : "comboBox", c->name, U::controlLeft, y, U::controlWidth, height,
                               {{s("fieldName"), c->name}, {s("rowSource"), c->lookup.table}}));
            y += height + U::gap;
        } else {
            out.append(control(id, "textBox", c->name, U::controlLeft, y, U::controlWidth, U::lineHeight,
                               {{s("fieldName"), c->name}}));
            y += U::lineHeight + U::gap;
        }
    }
    const int fullWidth = U::controlLeft + U::controlWidth - U::left;
    QList<FormControl> buttons;
    for (const FormControl &c : formControls(definition)) {
        if (c.kind == QLatin1String("button")) {
            buttons.append(c);
            continue;
        }
        if (c.kind != QLatin1String("subform")) {
            continue;
        }
        out.append(control(ids.take(c.id + s("_label"), s("label")), "label", c.caption + s("_Label"), U::left, y,
                           fullWidth, U::lineHeight, {{s("caption"), c.caption}}));
        y += U::lineHeight + U::gap;
        const QString form = formOverTable(file, c.table);
        out.append(control(c.id, "subform", c.caption, U::left, y, fullWidth, U::subformHeight,
                           {{s("formName"), form.isEmpty() ? c.table : form},
                            {s("linkMasterFields"), c.linkParent},
                            {s("linkChildFields"), c.linkChild}}));
        y += U::subformHeight + U::gap;
    }
    for (int i = 0; i < buttons.size(); ++i) {
        const int column = i % U::buttonsPerRow;
        if (column == 0 && i > 0) {
            y += U::lineHeight + U::gap;
        }
        QVariantMap properties = buttonAction(buttons.at(i));
        properties.insert(s("caption"), buttons.at(i).caption);
        out.append(control(buttons.at(i).id, "commandButton", buttons.at(i).caption,
                           U::left + column * (U::buttonWidth + U::gap), y, U::buttonWidth, U::lineHeight, properties));
    }
    return out;
}

LookupItems lookupItems(const BaseFile &file, const ColumnInfo &column, int limit)
{
    LookupItems out;
    if (!isLookupColumn(column)) {
        return out;
    }
    const QList<QPair<QVariant, QString>> choices = Records::lookupChoices(file, column.lookup, limit, column.type);
    QHash<QString, int> shown;
    for (const auto &[key, text] : choices) {
        ++shown[text];
    }
    for (const auto &[key, text] : choices) {
        out.labels.append(shown.value(text) > 1 ? s("%1 (%2)").arg(text, key.toString()) : text);
        out.keys.append(key);
    }
    return out;
}

QString lookupLabel(const BaseFile &file, const ColumnInfo &column, const QVariant &key)
{
    if (key.isValid() && !key.isNull() && column.lookup.isValueList()) {
        for (const auto &[value, text] : Records::lookupChoices(file, column.lookup, 20000, column.type)) {
            if (value == key) {
                return text;
            }
        }
        return displayText(column.type, key, file.currency());
    }
    if (!key.isValid() || key.isNull() || !isLookupColumn(column) || !file.tableExists(column.lookup.table)) {
        return key.isValid() && !key.isNull() ? key.toString() : QString();
    }
    const LookupDef &l = column.lookup;
    const QVariant raw = file.db().scalar(
        s("SELECT %1 FROM %2 WHERE %3 = ? LIMIT 1").arg(q(l.displayField), q(l.table), q(l.valueField)), {key});
    if (!raw.isValid() || raw.isNull()) {
        return key.toString(); // a key no related record has shows itself
    }
    const FieldType type = fieldTypes(file, l.table).value(l.displayField, FieldType::Text);
    const QString text = displayText(type, raw, file.currency());
    const int same = file.db().scalar(s("SELECT count(*) FROM %1 WHERE %2 = ?").arg(q(l.table), q(l.displayField)), {raw}).toInt();
    return same > 1 ? s("%1 (%2)").arg(text, key.toString()) : text;
}

ControlValue evaluateControlExpression(const BaseFile &file, const QString &table, const QString &expression,
                                       const QVariantMap &record)
{
    ControlValue out;
    QString text = expression.trimmed();
    if (text.startsWith(QLatin1Char('='))) {
        text = text.mid(1).trimmed(); // Access writes a control source as "=…"
    }
    if (text.isEmpty()) {
        out.ok = true;
        return out;
    }
    const QList<ColumnInfo> columns = Records::columns(file, table);
    if (columns.isEmpty()) {
        out.error = s("There is no table named %1.").arg(table);
        return out;
    }
    Expr::Context ctx;
    ctx.currency = file.currency();
    ctx.parameters = nullptr; // an unknown [name] is an error, never a prompt
    ctx.resolve = [&columns, &table](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        if (!owner.isEmpty() && owner.compare(table, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        const ColumnInfo *c = columnNamed(columns, name);
        if (c == nullptr || c->type == FieldType::File) {
            return std::nullopt;
        }
        return Expr::FieldRef{q(c->name), c->type};
    };
    const Expr::Compiled compiled = Expr::expression(text, ctx);
    if (!compiled.ok()) {
        out.error = compiled.error;
        return out;
    }
    if (compiled.aggregate) {
        out.error = s("A calculated control shows one record; it cannot total records.");
        return out;
    }
    // One row holding the record's storage values (each parsed by its field).
    QStringList values;
    QVariantList bindings;
    for (const ColumnInfo &c : columns) {
        QVariant stored;
        const QVariant shown = record.value(c.name);
        if (c.type != FieldType::File && shown.isValid() && !shown.isNull() && !shown.toString().isEmpty()) {
            const Parsed parsed = Records::resolveInput(file, c, shown);
            if (parsed.ok) {
                stored = parsed.value;
            }
        }
        values.append(s("? AS %1").arg(q(c.name)));
        bindings.append(stored);
    }
    QString error;
    const QList<QVariantList> rows =
        file.db().rows(s("SELECT %1 FROM (SELECT %2)").arg(compiled.sql, values.join(s(", "))), bindings, &error);
    if (!error.isEmpty() || rows.isEmpty()) {
        out.error = error.isEmpty() ? s("The expression has no value.") : error;
        return out;
    }
    const QVariant value = rows.first().value(0);
    FieldType type = compiled.type.value_or(FieldType::Text);
    if (!compiled.type) {
        const int id = value.typeId();
        type = id == QMetaType::LongLong || id == QMetaType::Int ? FieldType::Integer
             : id == QMetaType::Double                           ? FieldType::Number
                                                                 : FieldType::Text;
    }
    out.ok = true;
    out.text = value.isNull() ? QString() : displayText(type, value, file.currency());
    return out;
}

} // namespace QindaOffice::Base
