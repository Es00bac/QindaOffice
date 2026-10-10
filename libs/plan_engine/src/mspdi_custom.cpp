// SPDX-License-Identifier: GPL-3.0-or-later
// Task custom fields in Microsoft Project XML, and the adoption of what an
// M63–M82 .qplan kept verbatim that M83 models (mspdi_custom.h).
#include "mspdi_custom.h"

#include "custom_fields.h"

#include <QJsonArray>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace QindaOffice::Plan::Mspdi {

namespace {

// The ExtendedAttribute definition's children in the schema's order
// (mspdi_pj12.xsd, as MPXJ's JAXB classes list them).
const QStringList &definitionOrder()
{
    static const QStringList order{
        QStringLiteral("FieldID"),          QStringLiteral("FieldName"),       QStringLiteral("CFType"),
        QStringLiteral("Guid"),             QStringLiteral("ElemType"),        QStringLiteral("MaxMultiValues"),
        QStringLiteral("UserDef"),          QStringLiteral("Alias"),           QStringLiteral("SecondaryPID"),
        QStringLiteral("AutoRollDown"),     QStringLiteral("DefaultGuid"),     QStringLiteral("Ltuid"),
        QStringLiteral("SecondaryGuid"),    QStringLiteral("PhoneticAlias"),   QStringLiteral("RollupType"),
        QStringLiteral("CalculationType"),  QStringLiteral("Formula"),         QStringLiteral("RestrictValues"),
        QStringLiteral("ValuelistSortOrder"), QStringLiteral("AppendNewValues"), QStringLiteral("Default"),
        QStringLiteral("ValueList")};
    return order;
}

// A fragment's direct children, (name, element) in order.
QList<std::pair<QString, QString>> childrenOf(const QString &fragment)
{
    QList<std::pair<QString, QString>> out;
    QXmlStreamReader in(fragment);
    if (!in.readNextStartElement()) {
        return out;
    }
    while (in.readNextStartElement()) {
        const QString name = in.name().toString();
        out.append({name, capture(in)});
    }
    return out;
}

QString textOf(const QString &element)
{
    QXmlStreamReader in(element);
    return in.readNextStartElement() ? in.readElementText(QXmlStreamReader::SkipChildElements) : QString();
}

QList<CustomLookupEntry> lookupOf(const QString &valueList)
{
    QList<CustomLookupEntry> out;
    for (const auto &[name, xml] : childrenOf(valueList)) {
        if (name != QLatin1String("Value")) {
            continue;
        }
        CustomLookupEntry e;
        for (const auto &[child, text] : childrenOf(xml)) {
            if (child == QLatin1String("Value")) {
                e.value = textOf(text);
            } else if (child == QLatin1String("Description")) {
                e.description = textOf(text);
            }
        }
        out.append(e);
    }
    return out;
}

bool sameDefinition(const std::optional<CustomFieldDef> &file, const CustomFieldDef *model)
{
    const CustomFieldDef none;
    const CustomFieldDef &a = file ? *file : none;
    const CustomFieldDef &b = model != nullptr ? *model : none;
    return a.alias == b.alias && a.formula == b.formula && a.restrictToList == b.restrictToList && a.lookup == b.lookup;
}

void writeValue(QXmlStreamWriter &xml, qint64 fieldId, const QString &wire)
{
    const std::optional<CustomFieldInfo> info = customFieldByFieldId(fieldId);
    const bool duration = info && info->kind == CustomFieldKind::Duration;
    const qsizetype bar = duration ? wire.indexOf(QLatin1Char('|')) : -1;
    xml.writeStartElement(QStringLiteral("ExtendedAttribute"));
    xml.writeTextElement(QStringLiteral("FieldID"), QString::number(fieldId));
    xml.writeTextElement(QStringLiteral("Value"), bar < 0 ? wire : wire.left(bar));
    if (duration) {
        xml.writeTextElement(QStringLiteral("DurationFormat"), bar < 0 ? QStringLiteral("7") : wire.mid(bar + 1));
    }
    xml.writeEndElement();
}

// A definition written from the model, keeping what the file's had beyond it.
void writeDefinition(QXmlStreamWriter &xml, const CustomFieldInfo &info, const CustomFieldDef *model,
                     const QString &fragment)
{
    QList<std::pair<QString, QString>> parts;
    for (const auto &part : childrenOf(fragment)) {
        static const QStringList modelled{QStringLiteral("FieldID"), QStringLiteral("FieldName"), QStringLiteral("Alias"),
                                          QStringLiteral("Formula"), QStringLiteral("RestrictValues"),
                                          QStringLiteral("ValueList")};
        if (!modelled.contains(part.first)) {
            parts.append(part);
        }
    }
    const auto element = [](const QString &name, const QString &text) {
        QString out;
        QXmlStreamWriter w(&out);
        w.writeTextElement(name, text);
        return out;
    };
    parts.append({QStringLiteral("FieldID"), element(QStringLiteral("FieldID"), QString::number(info.fieldId))});
    parts.append({QStringLiteral("FieldName"), element(QStringLiteral("FieldName"), info.field)});
    if (model != nullptr && !model->alias.isEmpty()) {
        parts.append({QStringLiteral("Alias"), element(QStringLiteral("Alias"), model->alias)});
    }
    if (model != nullptr && !model->formula.isEmpty()) {
        parts.append({QStringLiteral("Formula"), element(QStringLiteral("Formula"), model->formula)});
    }
    if (model != nullptr && !model->lookup.isEmpty()) {
        parts.append({QStringLiteral("RestrictValues"),
                      element(QStringLiteral("RestrictValues"), model->restrictToList ? QStringLiteral("1") : QStringLiteral("0"))});
        QString list;
        QXmlStreamWriter w(&list);
        w.writeStartElement(QStringLiteral("ValueList"));
        for (int k = 0; k < model->lookup.size(); ++k) {
            w.writeStartElement(QStringLiteral("Value"));
            w.writeTextElement(QStringLiteral("ID"), QString::number(k + 1));
            w.writeTextElement(QStringLiteral("Value"), model->lookup.at(k).value);
            if (!model->lookup.at(k).description.isEmpty()) {
                w.writeTextElement(QStringLiteral("Description"), model->lookup.at(k).description);
            }
            w.writeEndElement();
        }
        w.writeEndElement();
        parts.append({QStringLiteral("ValueList"), list});
    }
    const auto rank = [](const QString &name) {
        const qsizetype r = definitionOrder().indexOf(name);
        return r < 0 ? definitionOrder().size() : r;
    };
    std::stable_sort(parts.begin(), parts.end(), [&](const auto &a, const auto &b) { return rank(a.first) < rank(b.first); });
    xml.writeStartElement(QStringLiteral("ExtendedAttribute"));
    for (const auto &part : std::as_const(parts)) {
        replay(xml, part.second);
    }
    xml.writeEndElement();
}

// Takes the kept elements called `name` out of an extra's kept list.
QStringList takeKept(QJsonObject &extra, const QString &name)
{
    QStringList taken;
    QList<Kept> rest;
    for (const Kept &k : keptFromJson(extra.value(kKeptKey))) {
        if (k.name == name) {
            taken.append(k.xml);
        } else {
            rest.append(k);
        }
    }
    if (taken.isEmpty()) {
        return taken;
    }
    if (rest.isEmpty()) {
        extra.remove(kKeptKey);
    } else {
        extra.insert(kKeptKey, keptToJson(rest));
    }
    return taken;
}

// A formula QindaPlan cannot evaluate for this task (a function it lacks):
// Project's value stays in the file.
bool unevaluated(const Project &project, int index, const QString &field)
{
    const CustomFieldDef *d = customFieldDef(project, field);
    return d != nullptr && !d->formula.isEmpty() && !evaluateFormula(project, index, d->formula).ok;
}

bool truth(const QString &element)
{
    const QString t = textOf(element).trimmed();
    return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

} // namespace

CustomValueXml parseCustomValue(const QString &fragment)
{
    CustomValueXml out;
    QString value;
    QString format;
    for (const auto &[name, xml] : childrenOf(fragment)) {
        if (name == QLatin1String("FieldID")) {
            out.fieldId = textOf(xml).trimmed().toLongLong();
        } else if (name == QLatin1String("Value")) {
            value = textOf(xml);
        } else if (name == QLatin1String("DurationFormat")) {
            format = textOf(xml).trimmed();
        }
    }
    const std::optional<CustomFieldInfo> info = customFieldByFieldId(out.fieldId);
    if (!value.isEmpty()) {
        out.wire = info && info->kind == CustomFieldKind::Duration
                       ? value + QLatin1Char('|') + (format.isEmpty() ? QStringLiteral("7") : format)
                       : value;
    }
    return out;
}

std::optional<CustomFieldDef> parseCustomDefinition(const QString &fragment, qint64 *fieldId)
{
    CustomFieldDef d;
    qint64 id = 0;
    for (const auto &[name, xml] : childrenOf(fragment)) {
        if (name == QLatin1String("FieldID")) {
            id = textOf(xml).trimmed().toLongLong();
        } else if (name == QLatin1String("Alias")) {
            d.alias = textOf(xml);
        } else if (name == QLatin1String("Formula")) {
            d.formula = textOf(xml);
        } else if (name == QLatin1String("RestrictValues")) {
            d.restrictToList = truth(xml);
        } else if (name == QLatin1String("ValueList")) {
            d.lookup = lookupOf(xml);
        }
    }
    if (fieldId != nullptr) {
        *fieldId = id;
    }
    const std::optional<CustomFieldInfo> info = customFieldByFieldId(id);
    if (!info) {
        return std::nullopt;
    }
    d.field = info->field;
    return d;
}

void readCustomValue(Task &task, const QString &fragment)
{
    const CustomValueXml v = parseCustomValue(fragment);
    QJsonArray list = task.extra.value(kCustomValuesKey).toArray();
    list.append(QJsonObject{{QStringLiteral("id"), QString::number(v.fieldId)}, {QStringLiteral("xml"), fragment}});
    task.extra.insert(kCustomValuesKey, list);
    const std::optional<CustomFieldInfo> info = customFieldByFieldId(v.fieldId);
    if (info && !v.wire.isEmpty() && !task.custom.contains(info->field)) {
        task.custom.insert(info->field, v.wire);
    }
}

void readCustomDefinitions(const QString &extendedAttributes, ProjectInfo &info, QList<CustomFieldDef> *defs)
{
    QJsonArray list;
    for (const auto &[name, xml] : childrenOf(extendedAttributes)) {
        qint64 id = 0;
        const std::optional<CustomFieldDef> d = name == QLatin1String("ExtendedAttribute")
                                                    ? parseCustomDefinition(xml, &id) : std::nullopt;
        list.append(QJsonObject{{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("xml"), xml}});
        const bool modelled = d && (!d->alias.isEmpty() || !d->lookup.isEmpty() || !d->formula.isEmpty());
        if (modelled) {
            const bool seen = std::any_of(defs->cbegin(), defs->cend(), [&](const CustomFieldDef &x) { return x.field == d->field; });
            if (!seen) {
                defs->append(*d);
            }
        }
    }
    info.extra.insert(kCustomDefsKey, list);
}

void writeCustomValues(Children &c, QXmlStreamWriter &xml, const Project &project, int taskIndex)
{
    const Task &t = project.tasks.at(taskIndex);
    QSet<QString> written;
    for (const QJsonValue &v : t.extra.value(kCustomValuesKey).toArray()) {
        const QString fragment = v.toObject().value(QStringLiteral("xml")).toString();
        const qint64 id = v.toObject().value(QStringLiteral("id")).toString().toLongLong();
        const std::optional<CustomFieldInfo> info = customFieldByFieldId(id);
        if (!info) {
            c.add(QStringLiteral("ExtendedAttribute"), [&xml, fragment] { replay(xml, fragment); });
            continue;
        }
        if (written.contains(info->field)) {
            continue; // a field the file gave twice: the first is the value
        }
        written.insert(info->field);
        const QString now = customFieldWireValue(project, taskIndex, info->field);
        if (now.isEmpty() && unevaluated(project, taskIndex, info->field)) {
            c.add(QStringLiteral("ExtendedAttribute"), [&xml, fragment] { replay(xml, fragment); });
            continue;
        }
        if (now.isEmpty()) {
            continue; // cleared here
        }
        if (parseCustomValue(fragment).wire == now) {
            c.add(QStringLiteral("ExtendedAttribute"), [&xml, fragment] { replay(xml, fragment); });
        } else {
            c.add(QStringLiteral("ExtendedAttribute"), [&xml, id, now] { writeValue(xml, id, now); });
        }
    }
    QStringList fields = t.custom.keys();
    for (const CustomFieldDef &d : project.customFields) {
        if (!d.formula.isEmpty() && !fields.contains(d.field)) {
            fields.append(d.field);
        }
    }
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        if (!fields.contains(info.field) || written.contains(info.field)) {
            continue;
        }
        const QString now = customFieldWireValue(project, taskIndex, info.field);
        if (!now.isEmpty()) {
            c.add(QStringLiteral("ExtendedAttribute"), [&xml, id = info.fieldId, now] { writeValue(xml, id, now); });
        }
    }
}

void writeCustomDefinitions(Children &c, QXmlStreamWriter &xml, const Project &project)
{
    const QStringList inUse = customFieldsInUse(project);
    if (!project.info.extra.contains(kCustomDefsKey) && inUse.isEmpty()) {
        return;
    }
    const QJsonArray kept = project.info.extra.value(kCustomDefsKey).toArray();
    c.add(QStringLiteral("ExtendedAttributes"), [&xml, &project, kept, inUse] {
        xml.writeStartElement(QStringLiteral("ExtendedAttributes"));
        QSet<QString> done;
        for (const QJsonValue &v : kept) {
            const QString fragment = v.toObject().value(QStringLiteral("xml")).toString();
            qint64 id = 0;
            const std::optional<CustomFieldDef> fromFile = parseCustomDefinition(fragment, &id);
            const std::optional<CustomFieldInfo> info = customFieldByFieldId(id);
            if (!info || done.contains(info->field)) {
                replay(xml, fragment);
                continue;
            }
            done.insert(info->field);
            const CustomFieldDef *model = customFieldDef(project, info->field);
            if (sameDefinition(fromFile, model)) {
                replay(xml, fragment);
            } else {
                writeDefinition(xml, *info, model, fragment);
            }
        }
        for (const QString &field : inUse) {
            if (!done.contains(field)) {
                writeDefinition(xml, *customFieldInfo(field), customFieldDef(project, field), QString());
            }
        }
        xml.writeEndElement();
    });
}

int adoptM83Elements(Project &project)
{
    int adopted = 0;
    ProjectInfo &info = project.info;
    if (!info.extra.contains(kCustomDefsKey)) {
        const QStringList defs = takeKept(info.extra, QStringLiteral("ExtendedAttributes"));
        if (!defs.isEmpty()) {
            readCustomDefinitions(defs.first(), info, &project.customFields);
            ++adopted;
        }
    }
    for (const char *name : {"DefaultTaskType", "NewTasksEffortDriven", "EditableActualCosts"}) {
        const QStringList kept = takeKept(info.extra, QLatin1String(name));
        if (kept.isEmpty()) {
            continue;
        }
        ++adopted;
        if (qstrcmp(name, "DefaultTaskType") == 0) {
            info.defaultTaskType = TaskType(std::clamp(textOf(kept.first()).trimmed().toInt(), 0, 2));
        } else if (qstrcmp(name, "NewTasksEffortDriven") == 0) {
            info.newTasksEffortDriven = truth(kept.first());
        } else {
            info.editableActualCosts = truth(kept.first());
        }
    }
    for (Task &t : project.tasks) {
        if (const QStringList type = takeKept(t.extra, QStringLiteral("Type")); !type.isEmpty()) {
            t.type = TaskType(std::clamp(textOf(type.first()).trimmed().toInt(), 0, 2));
            ++adopted;
        }
        if (const QStringList effort = takeKept(t.extra, QStringLiteral("EffortDriven")); !effort.isEmpty()) {
            t.effortDriven = truth(effort.first());
            ++adopted;
        }
        t.effortDriven = t.effortDriven || t.type == TaskType::FixedWork;
        if (const QStringList recurring = takeKept(t.extra, QStringLiteral("Recurring")); !recurring.isEmpty()) {
            t.recurring = truth(recurring.first());
            ++adopted;
        }
        if (!t.extra.contains(kCustomValuesKey)) {
            for (const QString &fragment : takeKept(t.extra, QStringLiteral("ExtendedAttribute"))) {
                readCustomValue(t, fragment);
                ++adopted;
            }
        }
    }
    return adopted;
}

} // namespace QindaOffice::Plan::Mspdi
