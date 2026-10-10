// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs as maps for QML and back (split from base_schema_read.cpp
// in M86). The QML table designer edits these maps and hands them back to
// planTableDesign, so every member a design carries must survive the round
// trip, including those the designer never shows (derivedTriggers, link).
//
// AGENT-CONTRACT (with qml/TableDesignView.qml, FieldProperties.qml and
// IndexesDialog.qml): a calculated field (M86) is {"type": "calculated",
// "resultType": <type id, "" = from the expression>, "expression": …};
// every other field's "type" is its own type id.
#include "base_schema_p.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

} // namespace

QString refActionId(RefAction action)
{
    switch (action) {
    case RefAction::Restrict: return s("restrict");
    case RefAction::Cascade: return s("cascade");
    case RefAction::SetNull: return s("setNull");
    case RefAction::NoAction: break;
    }
    return s("noAction");
}

RefAction refActionFromId(const QString &id)
{
    if (id == QLatin1String("restrict")) {
        return RefAction::Restrict;
    }
    if (id == QLatin1String("cascade")) {
        return RefAction::Cascade;
    }
    if (id == QLatin1String("setNull")) {
        return RefAction::SetNull;
    }
    return RefAction::NoAction;
}

QVariantMap IndexDef::toMap() const
{
    QVariantList list;
    for (const IndexField &f : fields) {
        list.append(QVariantMap{{s("field"), f.field}, {s("descending"), f.descending}});
    }
    return {{s("name"), name},
            {s("originalName"), originalName},
            {s("fields"), list},
            {s("unique"), unique},
            {s("ignoreNulls"), ignoreNulls}};
}

IndexDef IndexDef::fromMap(const QVariantMap &map)
{
    IndexDef index;
    index.name = map.value(s("name")).toString().trimmed();
    index.originalName = map.value(s("originalName")).toString();
    for (const QVariant &v : map.value(s("fields")).toList()) {
        const QVariantMap m = v.toMap();
        const QString field = m.value(s("field")).toString().trimmed();
        if (!field.isEmpty()) {
            index.fields.append({field, m.value(s("descending")).toBool()});
        }
    }
    index.unique = map.value(s("unique")).toBool();
    index.ignoreNulls = map.value(s("ignoreNulls")).toBool();
    return index;
}

QVariantMap FieldDef::toMap() const
{
    QVariantMap out{{s("name"), name},
                    {s("originalName"), originalName},
                    {s("type"), calculated ? s("calculated") : typeId(type)},
                    {s("typeLabel"), calculated ? s("Calculated (%1)").arg(typeLabel(type)) : typeLabel(type)},
                    {s("resultType"), calculated && type != FieldType::Other ? typeId(type) : QString()},
                    {s("declared"), declared},
                    {s("primaryKey"), primaryKey},
                    {s("required"), required},
                    {s("defaultValue"), defaultValue},
                    {s("unique"), unique},
                    {s("indexed"), indexed},
                    {s("caption"), caption},
                    {s("description"), description},
                    {s("lookupTable"), lookup.table},
                    {s("lookupField"), lookup.valueField},
                    {s("lookupDisplay"), lookup.displayField},
                    {s("lookupValues"), lookup.values},
                    {s("limitToList"), lookup.limitToList},
                    {s("validationRule"), validationRule},
                    {s("validationText"), validationText},
                    {s("inputMask"), inputMask},
                    {s("expression"), expression},
                    {s("generatedSql"), generatedSql},
                    {s("allowZeroLength"), allowZeroLength}};
    return out;
}

FieldDef FieldDef::fromMap(const QVariantMap &map)
{
    FieldDef f;
    f.name = map.value(s("name")).toString().trimmed();
    f.originalName = map.value(s("originalName")).toString();
    const QString type = map.value(s("type")).toString();
    f.calculated = type == QLatin1String("calculated");
    // AGENT-NOTE: an empty result type is "from the expression"; Other marks
    // it until base_calculated.cpp works the type out while planning.
    f.type = f.calculated ? typeFromId(map.value(s("resultType")).toString()).value_or(FieldType::Other)
                          : typeFromId(type).value_or(FieldType::Text);
    f.declared = map.value(s("declared")).toString();
    f.primaryKey = map.value(s("primaryKey")).toBool();
    f.required = map.value(s("required")).toBool();
    f.defaultValue = map.value(s("defaultValue")).toString();
    f.unique = map.value(s("unique")).toBool();
    f.indexed = map.value(s("indexed")).toBool();
    f.caption = map.value(s("caption")).toString();
    f.description = map.value(s("description")).toString();
    f.lookup.table = map.value(s("lookupTable")).toString();
    f.lookup.valueField = map.value(s("lookupField")).toString();
    f.lookup.displayField = map.value(s("lookupDisplay")).toString();
    for (const QVariant &v : map.value(s("lookupValues")).toList()) {
        if (!v.toString().trimmed().isEmpty()) {
            f.lookup.values.append(v.toString().trimmed());
        }
    }
    f.lookup.limitToList = map.value(s("limitToList")).toBool();
    f.validationRule = map.value(s("validationRule")).toString().trimmed();
    f.validationText = map.value(s("validationText")).toString().trimmed();
    f.inputMask = map.value(s("inputMask")).toString().trimmed();
    f.expression = f.calculated ? map.value(s("expression")).toString().trimmed() : QString();
    f.generatedSql = f.calculated ? map.value(s("generatedSql")).toString() : QString();
    f.allowZeroLength = map.value(s("allowZeroLength"), true).toBool();
    return f;
}

QString Relationship::id() const
{
    return childTable + QLatin1Char('.') + childField + QLatin1Char('>') + parentTable + QLatin1Char('.') + parentField;
}

QVariantMap Relationship::toMap() const
{
    return {{s("id"), id()},
            {s("childTable"), childTable},
            {s("childField"), childField},
            {s("parentTable"), parentTable},
            {s("parentField"), parentField},
            {s("onDelete"), refActionId(onDelete)},
            {s("onUpdate"), refActionId(onUpdate)}};
}

Relationship Relationship::fromMap(const QVariantMap &map)
{
    Relationship r;
    r.childTable = map.value(s("childTable")).toString();
    r.childField = map.value(s("childField")).toString();
    r.parentTable = map.value(s("parentTable")).toString();
    r.parentField = map.value(s("parentField")).toString();
    r.onDelete = refActionFromId(map.value(s("onDelete")).toString());
    r.onUpdate = refActionFromId(map.value(s("onUpdate")).toString());
    return r;
}

bool Relationship::sameLink(const Relationship &other) const
{
    return childTable.compare(other.childTable, Qt::CaseInsensitive) == 0
           && childField.compare(other.childField, Qt::CaseInsensitive) == 0
           && parentTable.compare(other.parentTable, Qt::CaseInsensitive) == 0
           && parentField.compare(other.parentField, Qt::CaseInsensitive) == 0;
}

int TableDef::fieldIndex(const QString &name) const
{
    for (int i = 0; i < fields.size(); ++i) {
        if (fields.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

const FieldDef *TableDef::field(const QString &name) const
{
    const int i = fieldIndex(name);
    return i < 0 ? nullptr : &fields.at(i);
}

QStringList TableDef::primaryKey() const
{
    QStringList out;
    for (const FieldDef &f : fields) {
        if (f.primaryKey) {
            out.append(f.name);
        }
    }
    return out;
}

QVariantMap TableDef::toMap() const
{
    QVariantList fieldList;
    for (const FieldDef &f : fields) {
        fieldList.append(f.toMap());
    }
    QVariantList relationList;
    for (const Relationship &r : relationships) {
        relationList.append(r.toMap());
    }
    QVariantList indexList;
    for (const IndexDef &i : indexes) {
        indexList.append(i.toMap());
    }
    return {{s("name"), name},
            {s("originalName"), originalName},
            {s("fields"), fieldList},
            {s("relationships"), relationList},
            {s("autoIncrement"), autoIncrement},
            {s("withoutRowid"), withoutRowid},
            {s("complexKeys"), complexKeys},
            {s("otherIndexSql"), otherIndexSql},
            {s("triggerSql"), triggerSql},
            {s("validationRule"), validationRule},
            {s("validationText"), validationText},
            {s("derivedTriggers"), derivedTriggers},
            {s("indexes"), indexList},
            {s("dataMacros"), dataMacros},
            {s("link"), link}};
}

TableDef TableDef::fromMap(const QVariantMap &map)
{
    TableDef t;
    t.name = map.value(s("name")).toString().trimmed();
    t.originalName = map.value(s("originalName")).toString();
    for (const QVariant &v : map.value(s("fields")).toList()) {
        t.fields.append(FieldDef::fromMap(v.toMap()));
    }
    for (const QVariant &v : map.value(s("relationships")).toList()) {
        t.relationships.append(Relationship::fromMap(v.toMap()));
    }
    t.autoIncrement = map.value(s("autoIncrement"), true).toBool();
    t.withoutRowid = map.value(s("withoutRowid")).toBool();
    t.complexKeys = map.value(s("complexKeys")).toBool();
    t.otherIndexSql = map.value(s("otherIndexSql")).toStringList();
    t.triggerSql = map.value(s("triggerSql")).toStringList();
    t.validationRule = map.value(s("validationRule")).toString().trimmed();
    t.validationText = map.value(s("validationText")).toString().trimmed();
    // "ruleTriggers" is M80's name for the same list (qbv: triggers only).
    t.derivedTriggers = map.contains(s("derivedTriggers")) ? map.value(s("derivedTriggers")).toStringList()
                                                           : map.value(s("ruleTriggers")).toStringList();
    for (const QVariant &v : map.value(s("indexes")).toList()) {
        t.indexes.append(IndexDef::fromMap(v.toMap()));
    }
    t.dataMacros = map.value(s("dataMacros")).toMap();
    t.link = map.value(s("link")).toMap();
    return t;
}

} // namespace QindaOffice::Base
