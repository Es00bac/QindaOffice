// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's client-side rules (Rules and Alerts) as values, their JSON
// import/export and the engine that decides what a message's rules do
// (M81, D-090; docs/mail.md#rules-and-alerts). Pure: the host builds the
// MessageFacts and carries out the RulePlan (moves through the account's
// server, forwards through the outgoing queue, …).
//
// AGENT-CONTRACT (organizer.json "rules", the export file, the rule
// editor's QML): a rule is {id, name, enabled, conditions [Condition],
// exceptions [Condition], actions [Action]}; an action is {type, folderKey,
// values, value} with
//   moveTo / copyTo     folderKey  acct:<account>:<mailbox>
//   delete              —          (to the account's Trash)
//   deletePermanently   —
//   markRead            —
//   flag                value: today | tomorrow | thisWeek | nextWeek | noDate
//   clearFlag           —
//   assignCategory      values: category names
//   clearCategories     —
//   setImportance       value: low | normal | high
//   forward / redirect  value: an address list ("Ada <ada@x>, b@y")
//   notify              value: the alert's text (empty: the rule's name)
//   stop                —          (stop processing more rules)
// Unknown keys are kept on rules and actions. The export file is
// {"schema": "qindaoffice.mail.rules/1", "rules": [...]}.
//
// Plan semantics (RulePlan): enabled rules run in order; a rule applies when
// all its conditions match and none of its exceptions does; its actions
// accumulate in order. The first move is THE move; a later move of another
// rule becomes a copy (Outlook files the message in both places); delete
// beats a move and deleting permanently beats both. `stop` ends the rule
// list after the current rule.

#include "mail_criteria.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct RuleAction {
    QString type;
    QString folderKey;
    QStringList values;
    QString value;
    QVariantMap extra;
};

struct MailRule {
    QString id;
    QString name;
    bool enabled = true;
    QList<Condition> conditions;
    QList<Condition> exceptions;
    QList<RuleAction> actions;
    QVariantMap extra;
};

struct RulePlan {
    QStringList matchedRules;       // ids, in order
    QStringList matchedNames;
    bool markRead = false;
    QString flag;                   // "" unchanged, a follow-up preset, or "clear"
    bool clearCategories = false;
    QStringList addCategories;      // after clearing, when both
    QString importance;             // "" unchanged
    QStringList copyTo;             // folder keys, in order, without the move target
    QString moveTo;                 // folder key, "" when the message stays
    bool deleteMessage = false;
    bool deletePermanently = false;
    QStringList forwardTo;          // one address list per forward action
    QStringList redirectTo;
    QStringList alerts;             // notify texts
    bool stopped = false;

    [[nodiscard]] bool isEmpty() const { return matchedRules.isEmpty(); }
    // True when the plan changes where the message lives.
    [[nodiscard]] bool disposes() const { return deletePermanently || deleteMessage || !moveTo.isEmpty(); }
};

namespace Rules {

inline constexpr const char kExportSchema[] = "qindaoffice.mail.rules/1";
inline constexpr int kMaxRules = 512;

// Every action type, in the order the editor lists them.
const QStringList &actionTypes();
const QStringList &flagPresets();       // today tomorrow thisWeek nextWeek noDate

MailRule fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const MailRule &rule);
RuleAction actionFromVariantMap(const QVariantMap &map);
QVariantMap actionToVariantMap(const RuleAction &action);
QList<MailRule> listFromVariant(const QVariant &list);
QVariantList listToVariant(const QList<MailRule> &rules);

// One line per problem, prefixed with the field path ("name: …",
// "actions[1].value: …"). Forward and redirect addresses must parse as the
// composer's (QindaTK Mime::parseAddressList).
QStringList validate(const MailRule &rule);

// True when the rule's conditions (or exceptions) read the header block.
bool needsHeaders(const MailRule &rule);
bool needsHeaders(const QList<MailRule> &rules);

bool applies(const MailRule &rule, const MessageFacts &facts, const CriteriaContext &context);
// `onlyRuleIds` limits the run to those rules (Run Rules Now); disabled
// rules are skipped either way unless `includeDisabled`.
RulePlan plan(const QList<MailRule> &rules, const MessageFacts &facts, const CriteriaContext &context,
              const QStringList &onlyRuleIds = {}, bool includeDisabled = false);

// The rule's sentence for the list ("Apply this rule after the message
// arrives from grace@example.org, move it to Projects").
QString summary(const MailRule &rule);

QByteArray exportJson(const QList<MailRule> &rules);
struct ImportResult {
    bool ok = false;
    QString error;
    QList<MailRule> rules;
    QStringList problems;           // per rule, "<name>: <problem>"
};
// Rules keep their order; each gets a fresh id; a rule with a problem
// (bad condition, a folder this computer does not know is the host's
// check) comes in turned off.
ImportResult importJson(const QByteArray &json);

} // namespace Rules
} // namespace QindaOffice::Mail
