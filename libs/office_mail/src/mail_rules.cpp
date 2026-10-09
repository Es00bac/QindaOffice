// SPDX-License-Identifier: GPL-3.0-or-later
// Rules: values, validation, the plan, JSON import/export (mail_rules.h).
#include "mail_rules.h"

#include <qindatk/mail_mime.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace QindaOffice::Mail::Rules {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::Rules", text);
}

const QStringList &ruleKeys()
{
    static const QStringList keys{u"id"_s, u"name"_s, u"enabled"_s, u"conditions"_s, u"exceptions"_s, u"actions"_s};
    return keys;
}

const QStringList &actionKeys()
{
    static const QStringList keys{u"type"_s, u"folderKey"_s, u"values"_s, u"value"_s};
    return keys;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// The mailbox part of a folder key, for sentences ("acct:a:Projects/2026" → "Projects/2026").
QString folderLabel(const QString &key)
{
    const qsizetype second = key.indexOf(u':', key.indexOf(u':') + 1);
    return second < 0 ? key : key.mid(second + 1);
}

QString describeAction(const RuleAction &a)
{
    const QString &t = a.type;
    if (t == "moveTo"_L1) return tr("move it to the “%1” folder").arg(folderLabel(a.folderKey));
    if (t == "copyTo"_L1) return tr("copy it to the “%1” folder").arg(folderLabel(a.folderKey));
    if (t == "delete"_L1) return tr("delete it");
    if (t == "deletePermanently"_L1) return tr("permanently delete it");
    if (t == "markRead"_L1) return tr("mark it as read");
    if (t == "flag"_L1) return tr("flag it for follow up (%1)").arg(a.value);
    if (t == "clearFlag"_L1) return tr("clear its flag");
    if (t == "assignCategory"_L1) return tr("assign it to %1").arg(a.values.join(u", "_s));
    if (t == "clearCategories"_L1) return tr("clear its categories");
    if (t == "setImportance"_L1) return tr("mark it as %1 importance").arg(a.value);
    if (t == "forward"_L1) return tr("forward it to %1").arg(a.value);
    if (t == "redirect"_L1) return tr("redirect it to %1").arg(a.value);
    if (t == "notify"_L1) return tr("display a desktop alert");
    if (t == "stop"_L1) return tr("stop processing more rules");
    return t;
}

} // namespace

const QStringList &actionTypes()
{
    static const QStringList all{u"moveTo"_s,   u"copyTo"_s,         u"delete"_s,         u"deletePermanently"_s,
                                 u"markRead"_s, u"flag"_s,           u"clearFlag"_s,      u"assignCategory"_s,
                                 u"clearCategories"_s, u"setImportance"_s, u"forward"_s,  u"redirect"_s,
                                 u"notify"_s,   u"stop"_s};
    return all;
}

const QStringList &flagPresets()
{
    static const QStringList presets{u"today"_s, u"tomorrow"_s, u"thisWeek"_s, u"nextWeek"_s, u"noDate"_s};
    return presets;
}

RuleAction actionFromVariantMap(const QVariantMap &map)
{
    RuleAction a;
    a.type = map.value(u"type"_s).toString().trimmed();
    a.folderKey = map.value(u"folderKey"_s).toString();
    const QVariant values = map.value(u"values"_s);
    a.values = values.typeId() == QMetaType::QString ? QStringList{values.toString()} : values.toStringList();
    a.values.removeAll(QString());
    a.value = map.value(u"value"_s).toString();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!actionKeys().contains(it.key())) a.extra.insert(it.key(), it.value());
    }
    return a;
}

QVariantMap actionToVariantMap(const RuleAction &a)
{
    QVariantMap map = a.extra;
    map.insert(u"type"_s, a.type);
    if (!a.folderKey.isEmpty()) map.insert(u"folderKey"_s, a.folderKey);
    if (!a.values.isEmpty() || a.type == "assignCategory"_L1) map.insert(u"values"_s, a.values);
    if (!a.value.isEmpty()) map.insert(u"value"_s, a.value);
    return map;
}

MailRule fromVariantMap(const QVariantMap &map)
{
    MailRule rule;
    rule.id = map.value(u"id"_s).toString().trimmed();
    rule.name = map.value(u"name"_s).toString().trimmed();
    rule.enabled = map.value(u"enabled"_s, true).toBool();
    rule.conditions = Criteria::listFromVariant(map.value(u"conditions"_s));
    rule.exceptions = Criteria::listFromVariant(map.value(u"exceptions"_s));
    for (const QVariant &value : map.value(u"actions"_s).toList()) rule.actions.append(actionFromVariantMap(value.toMap()));
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!ruleKeys().contains(it.key())) rule.extra.insert(it.key(), it.value());
    }
    return rule;
}

QVariantMap toVariantMap(const MailRule &rule)
{
    QVariantMap map = rule.extra;
    map.insert(u"id"_s, rule.id);
    map.insert(u"name"_s, rule.name);
    map.insert(u"enabled"_s, rule.enabled);
    map.insert(u"conditions"_s, Criteria::listToVariant(rule.conditions));
    map.insert(u"exceptions"_s, Criteria::listToVariant(rule.exceptions));
    QVariantList actions;
    for (const RuleAction &action : rule.actions) actions.append(actionToVariantMap(action));
    map.insert(u"actions"_s, actions);
    return map;
}

QList<MailRule> listFromVariant(const QVariant &list)
{
    QList<MailRule> out;
    for (const QVariant &value : list.toList()) out.append(fromVariantMap(value.toMap()));
    return out;
}

QVariantList listToVariant(const QList<MailRule> &rules)
{
    QVariantList out;
    for (const MailRule &rule : rules) out.append(toVariantMap(rule));
    return out;
}

QStringList validate(const MailRule &rule)
{
    QStringList problems;
    if (rule.name.isEmpty()) problems.append(u"name: "_s + tr("give the rule a name"));
    else if (rule.name.size() > 256) problems.append(u"name: "_s + tr("at most 256 characters"));
    if (rule.conditions.size() > 64 || rule.exceptions.size() > 64 || rule.actions.size() > 64)
        problems.append(u"actions: "_s + tr("at most 64 conditions, exceptions and actions each"));
    for (qsizetype i = 0; i < rule.conditions.size(); ++i)
        problems += Criteria::validate(rule.conditions.at(i), u"conditions[%1]"_s.arg(i));
    for (qsizetype i = 0; i < rule.exceptions.size(); ++i)
        problems += Criteria::validate(rule.exceptions.at(i), u"exceptions[%1]"_s.arg(i));
    bool acting = false;
    for (qsizetype i = 0; i < rule.actions.size(); ++i) {
        const RuleAction &a = rule.actions.at(i);
        const QString path = u"actions[%1]"_s.arg(i);
        if (!actionTypes().contains(a.type)) {
            problems.append(path + u".type: "_s + tr("unknown action “%1”").arg(a.type));
            continue;
        }
        acting = acting || a.type != "stop"_L1;
        if ((a.type == "moveTo"_L1 || a.type == "copyTo"_L1)
            && !(a.folderKey.startsWith("acct:"_L1) && a.folderKey.count(u':') >= 2))
            problems.append(path + u".folderKey: "_s + tr("choose a folder"));
        if (a.type == "flag"_L1 && !flagPresets().contains(a.value))
            problems.append(path + u".value: "_s + tr("choose when to follow up"));
        if (a.type == "assignCategory"_L1 && a.values.isEmpty())
            problems.append(path + u".values: "_s + tr("choose at least one category"));
        if (a.type == "setImportance"_L1 && !QStringList{u"low"_s, u"normal"_s, u"high"_s}.contains(a.value))
            problems.append(path + u".value: "_s + tr("choose low, normal or high"));
        if (a.type == "forward"_L1 || a.type == "redirect"_L1) {
            QString error;
            const QList<QindaTK::Mime::Address> to = QindaTK::Mime::parseAddressList(a.value, &error);
            if (!error.isEmpty()) problems.append(path + u".value: "_s + error);
            else if (to.isEmpty()) problems.append(path + u".value: "_s + tr("add at least one address"));
            else if (to.size() > 100) problems.append(path + u".value: "_s + tr("at most 100 addresses"));
        }
        if (a.type == "notify"_L1 && a.value.size() > 1024)
            problems.append(path + u".value: "_s + tr("at most 1024 characters"));
    }
    if (!acting) problems.append(u"actions: "_s + tr("choose at least one action"));
    return problems;
}

bool needsHeaders(const MailRule &rule)
{
    return Criteria::needsHeaders(rule.conditions) || Criteria::needsHeaders(rule.exceptions);
}

bool needsHeaders(const QList<MailRule> &rules)
{
    for (const MailRule &rule : rules) {
        if (rule.enabled && needsHeaders(rule)) return true;
    }
    return false;
}

bool applies(const MailRule &rule, const MessageFacts &facts, const CriteriaContext &context)
{
    return Criteria::matchesAll(rule.conditions, facts, context)
        && !Criteria::matchesAny(rule.exceptions, facts, context);
}

RulePlan plan(const QList<MailRule> &rules, const MessageFacts &facts, const CriteriaContext &context,
              const QStringList &onlyRuleIds, bool includeDisabled)
{
    RulePlan plan;
    const auto addUnique = [](QStringList &list, const QString &value) {
        if (!value.isEmpty() && !list.contains(value)) list.append(value);
    };
    for (const MailRule &rule : rules) {
        if ((!rule.enabled && !includeDisabled) || (!onlyRuleIds.isEmpty() && !onlyRuleIds.contains(rule.id))) continue;
        if (!applies(rule, facts, context)) continue;
        plan.matchedRules.append(rule.id);
        plan.matchedNames.append(rule.name);
        bool stop = false;
        for (const RuleAction &a : rule.actions) {
            const QString &t = a.type;
            if (t == "moveTo"_L1) {
                if (plan.moveTo.isEmpty()) plan.moveTo = a.folderKey;
                else if (plan.moveTo != a.folderKey) addUnique(plan.copyTo, a.folderKey);
            } else if (t == "copyTo"_L1) {
                addUnique(plan.copyTo, a.folderKey);
            } else if (t == "delete"_L1) {
                plan.deleteMessage = true;
            } else if (t == "deletePermanently"_L1) {
                plan.deletePermanently = true;
            } else if (t == "markRead"_L1) {
                plan.markRead = true;
            } else if (t == "flag"_L1) {
                plan.flag = a.value;
            } else if (t == "clearFlag"_L1) {
                plan.flag = u"clear"_s;
            } else if (t == "assignCategory"_L1) {
                for (const QString &name : a.values) {
                    bool known = false;
                    for (const QString &have : std::as_const(plan.addCategories))
                        known = known || have.compare(name, Qt::CaseInsensitive) == 0;
                    if (!known && !name.trimmed().isEmpty()) plan.addCategories.append(name.trimmed());
                }
            } else if (t == "clearCategories"_L1) {
                plan.clearCategories = true;
                plan.addCategories.clear();
            } else if (t == "setImportance"_L1) {
                plan.importance = a.value;
            } else if (t == "forward"_L1) {
                plan.forwardTo.append(a.value);
            } else if (t == "redirect"_L1) {
                plan.redirectTo.append(a.value);
            } else if (t == "notify"_L1) {
                plan.alerts.append(a.value.trimmed().isEmpty() ? rule.name : a.value.trimmed());
            } else if (t == "stop"_L1) {
                stop = true;
            }
        }
        if (stop) {
            plan.stopped = true;
            break;
        }
    }
    plan.copyTo.removeAll(plan.moveTo);
    return plan;
}

QString summary(const MailRule &rule)
{
    QStringList conditions;
    for (const Condition &c : rule.conditions) conditions.append(Criteria::describe(c));
    QStringList actions;
    for (const RuleAction &a : rule.actions) actions.append(describeAction(a));
    QStringList exceptions;
    for (const Condition &c : rule.exceptions) exceptions.append(Criteria::describe(c));
    QString text = conditions.isEmpty() ? tr("Apply this rule after a message arrives")
                                        : tr("Apply this rule after a message arrives %1")
                                              .arg(conditions.join(tr(" and ")));
    if (!actions.isEmpty()) text += u", "_s + actions.join(u", "_s);
    if (!exceptions.isEmpty()) text += u", "_s + tr("except if %1").arg(exceptions.join(tr(" or ")));
    return text;
}

QByteArray exportJson(const QList<MailRule> &rules)
{
    QJsonObject root;
    root.insert(u"schema"_s, QString::fromLatin1(kExportSchema));
    root.insert(u"rules"_s, QJsonArray::fromVariantList(listToVariant(rules)));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ImportResult importJson(const QByteArray &json)
{
    ImportResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        result.error = tr("The file is not a rules file (%1).").arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(u"schema"_s).toString() != QLatin1String(kExportSchema)) {
        result.error = tr("The file is not a QindaMail rules file (schema %1).")
                           .arg(root.value(u"schema"_s).toString().left(80));
        return result;
    }
    const QVariantList list = root.value(u"rules"_s).toArray().toVariantList();
    if (list.size() > kMaxRules) {
        result.error = tr("The file holds more than %1 rules.").arg(kMaxRules);
        return result;
    }
    for (const QVariant &value : list) {
        MailRule rule = fromVariantMap(value.toMap());
        rule.id = newId();
        if (rule.name.isEmpty()) rule.name = tr("Imported rule");
        const QStringList problems = validate(rule);
        if (!problems.isEmpty()) {
            rule.enabled = false;
            for (const QString &problem : problems) result.problems.append(rule.name + u": "_s + problem);
        }
        result.rules.append(rule);
    }
    result.ok = true;
    return result;
}

} // namespace QindaOffice::Mail::Rules
