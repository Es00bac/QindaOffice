// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_agent.h"
#include "note_host.h"
#include "note_tools.h"
#include <QUuid>
namespace QindaOffice::Apps::Notes {
namespace {
QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return {{QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties}, {QStringLiteral("required"), required},
        {QStringLiteral("additionalProperties"), false}};
}
QJsonObject refused(const QString &message)
{
    return {{QStringLiteral("status"), QStringLiteral("validation_failed")},
        {QStringLiteral("message"), message}, {QStringLiteral("changed"), QJsonArray{}}};
}
}
QJsonObject noteEditsSchema()
{
    const QJsonObject text{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("maxLength"), 65536}};
    const QJsonObject index{{QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 499}};
    const auto op = [](const QString &id) {
        return QJsonObject{{QStringLiteral("enum"), QJsonArray{id}}};
    };
    const QJsonObject blockType{{QStringLiteral("enum"), QJsonArray{
        QStringLiteral("markdown"), QStringLiteral("text"), QStringLiteral("check")}}};
    QJsonArray variants{
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("replace_block"))},
            {QStringLiteral("block"), index}, {QStringLiteral("text"), text}},
            {QStringLiteral("op"), QStringLiteral("block"), QStringLiteral("text")}),
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("delete_block"))},
            {QStringLiteral("block"), index}}, {QStringLiteral("op"), QStringLiteral("block")}),
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("insert_block"))},
            {QStringLiteral("after"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                {QStringLiteral("minimum"), -1}, {QStringLiteral("maximum"), 499}}},
            {QStringLiteral("type"), blockType}, {QStringLiteral("text"), text},
            {QStringLiteral("checked"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
            {QStringLiteral("op"), QStringLiteral("text")}),
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("replace_page"))},
            {QStringLiteral("markdown"), text}}, {QStringLiteral("op"), QStringLiteral("markdown")}),
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("rename_page"))},
            {QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 512}}}},
            {QStringLiteral("op"), QStringLiteral("title")}),
        objectSchema({{QStringLiteral("op"), op(QStringLiteral("set_checked"))},
            {QStringLiteral("block"), index},
            {QStringLiteral("checked"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
            {QStringLiteral("op"), QStringLiteral("block"), QStringLiteral("checked")})};
    return objectSchema({{QStringLiteral("edits"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1},
        {QStringLiteral("maxItems"), 256},
        {QStringLiteral("items"), QJsonObject{{QStringLiteral("anyOf"), variants}}}}}},
        {QStringLiteral("edits")});
}
QJsonArray NoteAgent::actions() const
{
    const QString reason = blockedReason();
    const bool undoable = !m_undoRef.isEmpty() && m_undoRevision == undoRevisionToken();
    const auto descriptor = [&](const QString &id, const QString &description,
                                const QJsonObject &schema, bool available) {
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), schema},
            {QStringLiteral("enabled"), reason.isEmpty() && available},
            {QStringLiteral("disabled_reason"), !reason.isEmpty() ? reason
                : available ? QString() : QStringLiteral("No current agent edit can be undone")}};
    };
    return {descriptor(QStringLiteral("note.edit"), QStringLiteral(
        "Edit typed blocks/checklists or rename the current page as one validated undo step. "
        "Ink, images and session evidence survive; indices refer to the inspected revision."),
        noteEditsSchema(), true),
        descriptor(QStringLiteral("note.undo"), QStringLiteral(
            "Undo the named edit only while it is still the current page's latest change"),
            objectSchema({{QStringLiteral("undo_ref"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("maxLength"), 100}}}}, {QStringLiteral("undo_ref")}), undoable)};
}
QJsonObject NoteAgent::execute(const QString &action, const QJsonObject &arguments)
{
    const QString reason = blockedReason();
    if (!reason.isEmpty()) return refused(reason);
    if (action == QLatin1String("note.undo")) {
        if (m_undoRef.isEmpty() || m_undoRef != arguments.value(QStringLiteral("undo_ref")).toString()
            || m_undoRevision != undoRevisionToken())
            return refused(QStringLiteral("The named edit is no longer the current undo step"));
        m_tools->undo();
        m_undoRef.clear();
        return {{QStringLiteral("status"), QStringLiteral("undone")},
            {QStringLiteral("message"), QStringLiteral("Agent edit undone")},
            {QStringLiteral("result"), state()}};
    }
    if (action != QLatin1String("note.edit")) return refused(QStringLiteral("Unknown Note action"));
    static_cast<void>(m_tools->snapshot(60000));
    const auto result = m_tools->apply(arguments.value(QStringLiteral("edits")).toArray());
    if (result.applied == 0)
        return refused(result.errors.isEmpty() ? QStringLiteral("No content changed")
            : result.errors.join(QStringLiteral("; ")));
    m_undoRef = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_undoRevision = undoRevisionToken();
    return {{QStringLiteral("status"), QStringLiteral("completed")},
        {QStringLiteral("message"), result.changes.join(QStringLiteral("; "))},
        {QStringLiteral("changed"), QJsonArray::fromStringList(result.changes)},
        {QStringLiteral("undo_ref"), m_undoRef}, {QStringLiteral("result"), state()}};
}
}
