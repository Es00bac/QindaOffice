// SPDX-License-Identifier: GPL-3.0-or-later
// Search folders: presets, values, matching (search_folders.h).
#include "search_folders.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QUuid>

namespace QindaOffice::Mail::SearchFolders {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::SearchFolders", text);
}

Condition make(const QString &type, const QStringList &values = {})
{
    Condition c;
    c.type = type;
    c.values = values;
    return c;
}

QStringList listOf(const QVariant &argument)
{
    if (argument.typeId() == QMetaType::QString) {
        QStringList out;
        for (const QString &part : argument.toString().split(QRegularExpression(u"[,;]"_s), Qt::SkipEmptyParts))
            if (!part.trimmed().isEmpty()) out.append(part.trimmed());
        return out;
    }
    QStringList out;
    for (const QString &value : argument.toStringList())
        if (!value.trimmed().isEmpty()) out.append(value.trimmed());
    return out;
}

} // namespace

const QStringList &presets()
{
    static const QStringList all{u"unread"_s,     u"flagged"_s,     u"unreadOrFlagged"_s, u"important"_s,
                                 u"fromPeople"_s, u"toMe"_s,        u"attachments"_s,     u"categorized"_s,
                                 u"large"_s,      u"old"_s,         u"words"_s,           u"custom"_s};
    return all;
}

QString presetName(const QString &preset)
{
    if (preset == "unread"_L1) return tr("Unread Mail");
    if (preset == "flagged"_L1) return tr("For Follow Up");
    if (preset == "unreadOrFlagged"_L1) return tr("Unread or For Follow Up");
    if (preset == "important"_L1) return tr("Important Mail");
    if (preset == "fromPeople"_L1) return tr("Mail from Specific People");
    if (preset == "toMe"_L1) return tr("Mail Sent Directly to Me");
    if (preset == "attachments"_L1) return tr("Mail with Attachments");
    if (preset == "categorized"_L1) return tr("Categorized Mail");
    if (preset == "large"_L1) return tr("Large Mail");
    if (preset == "old"_L1) return tr("Old Mail");
    if (preset == "words"_L1) return tr("Mail with Specific Words");
    return tr("Custom Search Folder");
}

SearchFolder forPreset(const QString &preset, const QVariant &argument)
{
    SearchFolder folder;
    folder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    folder.preset = presets().contains(preset) ? preset : u"custom"_s;
    folder.name = presetName(folder.preset);
    const QString &p = folder.preset;
    if (p == "unread"_L1) {
        folder.conditions = {make(u"unread"_s)};
    } else if (p == "flagged"_L1) {
        folder.conditions = {make(u"flagged"_s)};
    } else if (p == "unreadOrFlagged"_L1) {
        folder.match = u"any"_s;
        folder.conditions = {make(u"unread"_s), make(u"flagged"_s)};
    } else if (p == "important"_L1) {
        Condition c = make(u"importance"_s);
        c.value = u"high"_s;
        folder.conditions = {c};
    } else if (p == "fromPeople"_L1) {
        folder.conditions = {make(u"from"_s, listOf(argument))};
    } else if (p == "toMe"_L1) {
        folder.conditions = {make(u"toMe"_s)};
    } else if (p == "attachments"_L1) {
        folder.conditions = {make(u"hasAttachment"_s)};
    } else if (p == "categorized"_L1) {
        folder.conditions = {make(u"category"_s, listOf(argument))};
    } else if (p == "large"_L1) {
        Condition c = make(u"size"_s);
        c.minimum = argument.isValid() && argument.toLongLong() > 0 ? argument.toLongLong() : 100;
        folder.conditions = {c};
    } else if (p == "old"_L1) {
        Condition c = make(u"olderThan"_s);
        c.days = argument.isValid() && argument.toInt() > 0 ? argument.toInt() : 7;
        folder.conditions = {c};
    } else if (p == "words"_L1) {
        folder.conditions = {make(u"subjectOrBody"_s, listOf(argument))};
    }
    return folder;
}

SearchFolder fromVariantMap(const QVariantMap &map)
{
    SearchFolder folder;
    folder.id = map.value(u"id"_s).toString().trimmed();
    folder.name = map.value(u"name"_s).toString().trimmed();
    folder.preset = map.value(u"preset"_s, u"custom"_s).toString();
    folder.match = map.value(u"match"_s).toString() == "any"_L1 ? u"any"_s : u"all"_s;
    folder.conditions = Criteria::listFromVariant(map.value(u"conditions"_s));
    folder.accounts = map.value(u"accounts"_s).toStringList();
    static const QStringList known{u"id"_s, u"name"_s, u"preset"_s, u"match"_s, u"conditions"_s, u"accounts"_s};
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!known.contains(it.key())) folder.extra.insert(it.key(), it.value());
    }
    return folder;
}

QVariantMap toVariantMap(const SearchFolder &folder)
{
    QVariantMap map = folder.extra;
    map.insert(u"id"_s, folder.id);
    map.insert(u"name"_s, folder.name);
    map.insert(u"preset"_s, folder.preset);
    map.insert(u"match"_s, folder.match);
    map.insert(u"conditions"_s, Criteria::listToVariant(folder.conditions));
    map.insert(u"accounts"_s, folder.accounts);
    return map;
}

QList<SearchFolder> listFromVariant(const QVariant &list)
{
    QList<SearchFolder> out;
    for (const QVariant &value : list.toList()) {
        const SearchFolder folder = fromVariantMap(value.toMap());
        if (!folder.id.isEmpty() && out.size() < kMaxFolders) out.append(folder);
    }
    return out;
}

QVariantList listToVariant(const QList<SearchFolder> &folders)
{
    QVariantList out;
    for (const SearchFolder &folder : folders) out.append(toVariantMap(folder));
    return out;
}

QStringList validate(const SearchFolder &folder)
{
    QStringList problems;
    if (folder.name.isEmpty()) problems.append(u"name: "_s + tr("give the search folder a name"));
    else if (folder.name.size() > 256) problems.append(u"name: "_s + tr("at most 256 characters"));
    if (folder.conditions.isEmpty()) problems.append(u"conditions: "_s + tr("choose what the folder shows"));
    for (qsizetype i = 0; i < folder.conditions.size(); ++i)
        problems += Criteria::validate(folder.conditions.at(i), u"conditions[%1]"_s.arg(i));
    return problems;
}

bool matches(const SearchFolder &folder, const MessageFacts &facts, const CriteriaContext &context)
{
    if (!folder.accounts.isEmpty() && !folder.accounts.contains(facts.accountId)) return false;
    if (folder.conditions.isEmpty()) return false;
    return folder.match == "any"_L1 ? Criteria::matchesAny(folder.conditions, facts, context)
                                    : Criteria::matchesAll(folder.conditions, facts, context);
}

} // namespace QindaOffice::Mail::SearchFolders
