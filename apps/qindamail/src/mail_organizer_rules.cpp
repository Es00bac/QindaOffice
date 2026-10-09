// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: rules — editing, import/export, new mail as it arrives
// and Run Rules Now (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "imap_extras.h"
#include "mail_service.h"
#include "provider_calls.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

QString pathOf(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

bool isInbox(const QString &mailbox)
{
    return mailbox.compare(u"INBOX"_s, Qt::CaseInsensitive) == 0;
}

} // namespace

QVariantList MailOrganizer::rules() const
{
    QVariantList out;
    for (const M::MailRule &rule : m_rules) {
        QVariantMap map = M::Rules::toVariantMap(rule);
        map.insert(u"summary"_s, M::Rules::summary(rule));
        out.append(map);
    }
    return out;
}

QVariantMap MailOrganizer::newRule() const
{
    M::MailRule rule;
    rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return M::Rules::toVariantMap(rule);
}

QVariantMap MailOrganizer::ruleFromMessage(const QString &messageId) const
{
    const QVariantMap message = m_service->messageById(messageId);
    QVariantMap rule = newRule();
    if (message.isEmpty()) return {{u"rule"_s, rule}, {u"suggestions"_s, QVariantMap()}};
    QString subject = message.value(u"subject"_s).toString().trimmed();
    static const QRegularExpression prefix(u"^((re|fwd?|aw|wg|sv|tr|rv)(\\[\\d+\\])?:\\s*)+"_s, QRegularExpression::CaseInsensitiveOption);
    subject.remove(prefix);
    const QString sender = message.value(u"senderAddress"_s).toString();
    QString me;
    const QString recipients = message.value(u"to"_s).toString() + u',' + message.value(u"cc"_s).toString();
    for (const QString &own : m_service->ownAddresses()) {
        if (recipients.contains(own, Qt::CaseInsensitive)) me = own;
    }
    // Outlook's Create Rule: the sender's condition on, the others offered.
    M::Condition from;
    from.type = u"from"_s;
    from.values = {sender};
    rule.insert(u"name"_s, sender.isEmpty() ? subject : sender);
    rule.insert(u"conditions"_s, M::Criteria::listToVariant({from}));
    return {{u"rule"_s, rule},
            {u"suggestions"_s, QVariantMap{{u"from"_s, QStringList{sender}}, {u"subject"_s, QStringList{subject}},
                                          {u"sentTo"_s, me.isEmpty() ? QStringList() : QStringList{me}},
                                          {u"account"_s, QStringList{message.value(u"accountId"_s).toString()}},
                                          {u"folderKey"_s, message.value(u"folderKey"_s).toString()}}}};
}

QStringList MailOrganizer::validateRule(const QVariantMap &map) const
{
    const M::MailRule rule = M::Rules::fromVariantMap(map);
    QStringList problems = M::Rules::validate(rule);
    for (qsizetype i = 0; i < rule.actions.size(); ++i) {
        const M::RuleAction &a = rule.actions.at(i);
        if ((a.type == "moveTo"_L1 || a.type == "copyTo"_L1) && !a.folderKey.isEmpty() && folderLabel(a.folderKey).isEmpty())
            problems.append(u"actions[%1].folderKey: "_s.arg(i) + tr("this computer has no such folder"));
    }
    return problems;
}

QString MailOrganizer::describeRule(const QVariantMap &rule) const
{
    return M::Rules::summary(M::Rules::fromVariantMap(rule));
}

QString MailOrganizer::folderLabel(const QString &folderKey) const
{
    const QString account = folderKey.section(u':', 1, 1);
    for (const QVariant &value : m_service->folderTargets(account)) {
        const QVariantMap target = value.toMap();
        if (target.value(u"key"_s).toString() == folderKey)
            return tr("%1 (%2)").arg(target.value(u"name"_s).toString(), m_service->accountName(account));
    }
    return {};
}

QVariantMap MailOrganizer::saveRule(const QVariantMap &map)
{
    M::MailRule rule = M::Rules::fromVariantMap(map);
    if (rule.id.isEmpty()) rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap withId = M::Rules::toVariantMap(rule);
    const QStringList errors = validateRule(withId);
    if (!errors.isEmpty()) return {{u"ok"_s, false}, {u"id"_s, rule.id}, {u"errors"_s, errors}};
    bool replaced = false;
    for (M::MailRule &existing : m_rules) {
        if (existing.id == rule.id) {
            existing = rule;
            replaced = true;
        }
    }
    if (!replaced) {
        if (m_rules.size() >= M::Rules::kMaxRules) return {{u"ok"_s, false}, {u"errors"_s, QStringList{tr("There are too many rules")}}};
        m_rules.append(rule);
    }
    save();
    bump();
    return {{u"ok"_s, true}, {u"id"_s, rule.id}, {u"errors"_s, QStringList()}};
}

bool MailOrganizer::deleteRule(const QString &id)
{
    const qsizetype before = m_rules.size();
    m_rules.removeIf([&id](const M::MailRule &rule) { return rule.id == id; });
    if (m_rules.size() == before) return false;
    save();
    bump();
    return true;
}

QString MailOrganizer::copyRule(const QString &id)
{
    for (qsizetype i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id != id) continue;
        M::MailRule copy = m_rules.at(i);
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.name = tr("Copy of %1").arg(copy.name);
        m_rules.insert(i + 1, copy);
        save();
        bump();
        return copy.id;
    }
    return {};
}

bool MailOrganizer::moveRule(const QString &id, int index)
{
    for (qsizetype i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id != id) continue;
        const qsizetype to = qBound(qsizetype(0), qsizetype(index), m_rules.size() - 1);
        if (to == i) return true;
        m_rules.move(i, to);
        save();
        bump();
        return true;
    }
    return false;
}

bool MailOrganizer::setRuleEnabled(const QString &id, bool enabled)
{
    for (M::MailRule &rule : m_rules) {
        if (rule.id != id) continue;
        if (enabled && !validateRule(M::Rules::toVariantMap(rule)).isEmpty()) return false;
        rule.enabled = enabled;
        save();
        bump();
        return true;
    }
    return false;
}

QVariantMap MailOrganizer::exportRules(const QUrl &url, const QStringList &ids) const
{
    QList<M::MailRule> chosen;
    for (const M::MailRule &rule : m_rules) {
        if (ids.isEmpty() || ids.contains(rule.id)) chosen.append(rule);
    }
    QSaveFile file(pathOf(url));
    const QByteArray bytes = M::Rules::exportJson(chosen);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    return {{u"ok"_s, ok}, {u"count"_s, int(chosen.size())},
            {u"error"_s, ok ? QString() : tr("Could not write %1").arg(pathOf(url))}};
}

QVariantMap MailOrganizer::importRules(const QUrl &url)
{
    QFile file(pathOf(url));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {{u"ok"_s, false}, {u"added"_s, 0}, {u"error"_s, tr("Could not read %1").arg(pathOf(url))}};
    M::Rules::ImportResult imported = M::Rules::importJson(file.readAll());
    if (!imported.ok) return {{u"ok"_s, false}, {u"added"_s, 0}, {u"error"_s, imported.error}};
    if (m_rules.size() + imported.rules.size() > M::Rules::kMaxRules)
        return {{u"ok"_s, false}, {u"added"_s, 0}, {u"error"_s, tr("There would be too many rules")}};
    QStringList problems = imported.problems;
    for (M::MailRule &rule : imported.rules) {
        // A folder of someone else's computer: the rule comes in turned off.
        const QStringList local = validateRule(M::Rules::toVariantMap(rule));
        if (rule.enabled && !local.isEmpty()) {
            rule.enabled = false;
            for (const QString &problem : local) problems.append(rule.name + u": "_s + problem);
        }
        m_rules.append(rule);
    }
    save();
    bump();
    return {{u"ok"_s, true}, {u"added"_s, int(imported.rules.size())}, {u"problems"_s, problems}, {u"error"_s, QString()}};
}

QString MailOrganizer::runRulesNow(const QStringList &ruleIds, const QString &folderKey, bool includeSubfolders,
                                   const QString &which)
{
    const QString requestId = ProviderCalls::newRequestId(u"rules"_s);
    QStringList keys{folderKey};
    if (includeSubfolders) {
        for (const QVariant &value : m_service->folderTargets(folderKey.section(u':', 1, 1))) {
            const QString key = value.toMap().value(u"key"_s).toString();
            if (key.startsWith(folderKey + u'/') || key.startsWith(folderKey + u'.')) keys.append(key);
        }
    }
    QList<Pending> items;
    for (const QString &key : std::as_const(keys)) {
        for (const QVariant &value : m_service->folderRecords(key)) {
            const QVariantMap row = value.toMap();
            if (row.value(u"isDraft"_s).toBool()) continue;
            const bool unread = row.value(u"unread"_s).toBool();
            if ((which == "read"_L1 && unread) || (which == "unread"_L1 && !unread)) continue;
            items.append({row.value(u"id"_s).toString(), requestId, ruleIds, true});
        }
    }
    m_runs.insert(requestId, {{u"processed"_s, 0}, {u"matched"_s, 0}, {u"pending"_s, int(items.size())}});
    if (items.isEmpty()) {
        finishRun(requestId);
    } else {
        enqueue(items);
    }
    return requestId;
}

void MailOrganizer::finishRun(const QString &requestId)
{
    const QVariantMap run = m_runs.take(requestId);
    answerLater("rules", requestId, {{u"ok"_s, true}, {u"state"_s, u"accepted"_s}, {u"error"_s, QString()},
                                     {u"processed"_s, run.value(u"processed"_s)}, {u"matched"_s, run.value(u"matched"_s)}});
}

void MailOrganizer::noteSyncResult(AccountRuntime *rt, const QString &mailbox, const QVariantMap &result)
{
    if (rt == nullptr || m_service == nullptr) return;
    const bool accepted = result.value(u"state"_s).toString() == "accepted"_L1;
    if (accepted && (isInbox(mailbox) || rt->isPop3())) {
        const QString key = rt->id() + u'\n' + (rt->isPop3() ? u"INBOX"_s : mailbox);
        const QStringList added = result.value(u"addedIds"_s).toStringList();
        if (result.value(u"reset"_s).toBool() || !m_baselines.contains(key)) {
            // The first download (or a rebuilt mailbox) is old mail: no rules.
            if (!m_baselines.contains(key)) {
                m_baselines.insert(key);
                save();
            }
        } else if (!added.isEmpty()) {
            QList<Pending> arrivals;
            for (const QString &id : added) arrivals.append({id, QString(), {}, false});
            enqueue(arrivals);
        }
    }
    if (accepted) pullTags(rt, mailbox, result);
    if (accepted && isInbox(mailbox)) syncCategoryList(rt);
}

void MailOrganizer::enqueue(const QList<Pending> &items)
{
    const bool was = running();
    m_queue += items;
    if (!was) emit runningChanged();
    if (!m_running) QTimer::singleShot(0, this, &MailOrganizer::processNext);
}

void MailOrganizer::processNext()
{
    if (m_running) return;
    if (m_queue.isEmpty()) {
        emit runningChanged();
        return;
    }
    m_running = true;
    const Pending item = m_queue.takeFirst();
    const QPointer<MailOrganizer> self(this);
    const auto next = [self, item](bool matched) {
        if (self == nullptr) return;
        if (!item.requestId.isEmpty() && self->m_runs.contains(item.requestId)) {
            QVariantMap &run = self->m_runs[item.requestId];
            run.insert(u"processed"_s, run.value(u"processed"_s).toInt() + 1);
            run.insert(u"matched"_s, run.value(u"matched"_s).toInt() + (matched ? 1 : 0));
            run.insert(u"pending"_s, run.value(u"pending"_s).toInt() - 1);
            if (run.value(u"pending"_s).toInt() <= 0) self->finishRun(item.requestId);
        }
        self->m_running = false;
        QTimer::singleShot(0, self, &MailOrganizer::processNext);
    };
    const QVariantMap message = record(item.messageId);
    if (!message.isEmpty() && item.requestId.isEmpty()
        && m_ignored.contains(message.value(u"threadId"_s).toString())) {
        // Ignore Conversation: what arrives in it goes straight to Trash.
        expect(m_service->deleteMessages({item.messageId}), [next](const QVariantMap &) { next(true); });
        return;
    }
    if (message.isEmpty() || m_rules.isEmpty()) {
        next(false);
        return;
    }
    const auto decide = [self, item, next](const QString &headers) {
        if (self == nullptr) return;
        const QVariantMap current = self->record(item.messageId);
        if (current.isEmpty()) {
            next(false);
            return;
        }
        const M::RulePlan plan = M::Rules::plan(self->m_rules, self->factsOf(current, headers), self->context(),
                                                item.onlyRules, item.includeDisabled);
        if (plan.isEmpty()) {
            next(false);
            return;
        }
        self->carryOut(item.messageId, plan, [next](const QVariantMap &) { next(true); });
    };
    // Header conditions: the stored source's header block, else the server's.
    QString headers;
    const QByteArray source = m_service->storedSource(item.messageId);
    if (!source.isEmpty()) headers = QString::fromUtf8(source.left(source.indexOf("\r\n\r\n")));
    AccountRuntime *rt = m_service->runtime(m_service->ownerAccountOf(message));
    const bool needed = M::Rules::needsHeaders(m_rules) || (item.includeDisabled && !item.onlyRules.isEmpty());
    if (!headers.isEmpty() || !needed || rt == nullptr || rt->imapExtras() == nullptr) {
        decide(headers);
        return;
    }
    ImapExtras *extras = rt->imapExtras();
    const QString mailbox = message.value(u"remoteMailbox"_s).toString();
    const qint64 validity = message.value(u"remoteUidValidity"_s).toLongLong();
    const qint64 uid = message.value(u"remoteUid"_s).toLongLong();
    rt->run(u"imap"_s, extras,
            [extras, mailbox, validity, uid](const QString &rid) { return extras->headers(rid, mailbox, validity, {uid}); },
            [decide, uid](const QVariantMap &result) {
                decide(result.value(u"headers"_s).toMap().value(QString::number(uid)).toString());
            },
            false);
}

} // namespace QindaOffice::Apps::Mail
