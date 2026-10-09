// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: tags coming back from the servers after a sync, the list
// rows' decoration, and flagged mail on the task board (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "imap_extras.h"
#include "mail_service.h"
#include "provider_calls.h"

#include "exchange_mailbox.h"

#include <qindatk/imap_mail_jobs.h>
#include <qindatk/local_productivity_store.h>

#include <QLocale>
#include <algorithm>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

constexpr int kRetryPerSync = 20;
constexpr int kMaxKeywordRecords = 5000;
const QString kFlagCard = QStringLiteral("mailflag:");

QString text(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).toString();
}

M::FollowUp followOf(const QVariantMap &record)
{
    M::FollowUp flag = M::FollowUps::fromVariantMap(record.value(u"followUp"_s).toMap());
    if (flag.status.isEmpty() && record.value(u"flagged"_s).toBool()) {
        flag.status = u"flagged"_s;
        flag.preset = u"noDate"_s;
        flag.column = u"todo"_s;
    }
    return flag;
}

} // namespace

void MailOrganizer::pullTags(AccountRuntime *rt, const QString &mailbox, const QVariantMap &)
{
    const QString owner = rt->id();
    QPointer<MailOrganizer> self(this);
    if (QindaOffice::Mail::ExchangeMailbox *box = rt->mailbox()) {
        if (mailbox.compare(u"INBOX"_s, Qt::CaseInsensitive) != 0 || m_mailboxBlocked.contains(owner)) return;
        const QString service = rt->mailboxService() == "graph"_L1 ? u"graphmail"_s : u"ews"_s;
        rt->run(service, box, [box](const QString &rid) { return box->pullItemStates(rid, 30); },
                [self, owner](const QVariantMap &result) {
                    if (self == nullptr) return;
                    if (result.value(u"ok"_s).toBool()) self->applyServerTags(owner, result.value(u"items"_s).toList());
                    else if (result.value(u"consentRequired"_s).toBool() || result.value(u"needsSignIn"_s).toBool())
                        self->m_mailboxBlocked.insert(owner);
                },
                false);
        return;
    }
    if (!rt->isImap() || !m_keywordSupport.value(owner, true)) return;
    QList<qint64> uids;
    qint64 validity = 0;
    for (const QVariant &value : m_service->store()->messages()) {
        const QVariantMap row = value.toMap();
        if (text(row, "remoteAccount") != owner || text(row, "remoteMailbox") != mailbox) continue;
        uids.append(row.value(u"remoteUid"_s).toLongLong());
        validity = row.value(u"remoteUidValidity"_s).toLongLong();
    }
    if (uids.isEmpty() || uids.size() > kMaxKeywordRecords) return;
    ImapExtras *extras = rt->imapExtras();
    rt->run(u"imap"_s, extras, [extras, mailbox, validity, uids](const QString &rid) {
        return extras->keywords(rid, mailbox, validity, uids);
    }, [self, owner, mailbox, validity](const QVariantMap &result) {
        if (self == nullptr || !result.value(u"ok"_s).toBool()) return;
        const QVariantMap keywords = result.value(u"keywords"_s).toMap();
        const QList<M::Category> known = self->listFor(owner);
        bool any = false;
        QHash<qint64, QStringList> names;
        for (auto it = keywords.cbegin(); it != keywords.cend(); ++it) {
            QStringList list;
            for (const QString &keyword : it.value().toStringList()) {
                const QString name = M::Categories::nameForKeyword(keyword, known);
                if (!name.isEmpty()) list.append(name);
            }
            any = any || !list.isEmpty();
            names.insert(it.key().toLongLong(), list);
        }
        // AGENT-GUARD: a server that cannot store keywords reports none —
        // that must not wipe the categories kept on this computer.
        if (!result.value(u"allowsKeywords"_s).toBool() && !any) {
            if (self->m_keywordSupport.value(owner, true)) {
                self->m_keywordSupport.insert(owner, false);
                self->save();
            }
            return;
        }
        QindaTK::LocalProductivityStore *store = self->m_service->store();
        bool changed = false;
        int retries = 0;
        for (auto it = names.cbegin(); it != names.cend(); ++it) {
            const QString id = QindaTK::ImapJobs::recordId(owner, mailbox, validity, it.key());
            QVariantMap row = self->record(id);
            if (row.isEmpty()) continue;
            if (row.value(u"tagsPending"_s).toBool()) {
                if (retries++ < kRetryPerSync) self->pushMessageTags(id, true, true, [](const QVariantMap &) {});
                continue;
            }
            const QStringList server = M::Categories::normalized(it.value());
            if (server == row.value(u"categories"_s).toStringList() && server == row.value(u"remoteCategories"_s).toStringList()) continue;
            row.insert(u"categories"_s, server);
            row.insert(u"remoteCategories"_s, server);
            changed = store->upsert(u"messages"_s, row).value(u"ok"_s).toBool() || changed;
        }
        if (changed) static_cast<void>(self->m_service->saveStore());
    }, false);
}

void MailOrganizer::applyServerTags(const QString &accountId, const QVariantList &items)
{
    QHash<QString, QVariantMap> byId;
    for (const QVariant &value : items) byId.insert(value.toMap().value(u"internetMessageId"_s).toString(), value.toMap());
    QindaTK::LocalProductivityStore *store = m_service->store();
    bool changed = false;
    int retries = 0;
    for (const QVariant &value : store->messages()) {
        QVariantMap row = value.toMap();
        if (m_service->ownerAccountOf(row) != accountId) continue;
        const auto found = byId.constFind(text(row, "internetMessageId"));
        if (found == byId.cend()) continue;
        if (row.value(u"tagsPending"_s).toBool()) {
            if (retries++ < kRetryPerSync) pushMessageTags(text(row, "id"), true, true, [](const QVariantMap &) {});
            continue;
        }
        const QVariantMap item = found.value();
        const QStringList names = M::Categories::normalized(item.value(u"categories"_s).toStringList());
        const QVariantMap server = item.value(u"flag"_s).toMap();
        M::FollowUp flag = followOf(row);
        const QString status = server.value(u"status"_s).toString();
        if (status.isEmpty()) {
            flag = {};
        } else {
            const QDate start = QDate::fromString(server.value(u"start"_s).toString(), Qt::ISODate);
            const QDate due = QDate::fromString(server.value(u"due"_s).toString(), Qt::ISODate);
            if (flag.status != status || flag.start != start || flag.due != due) {
                const QString column = flag.column;
                flag = due.isValid() ? M::FollowUps::custom(start, due) : M::FollowUps::forPreset(u"noDate"_s, today());
                flag.column = column.isEmpty() ? u"todo"_s : column;
                if (status == "complete"_L1)
                    flag = M::FollowUps::completed(flag, QDateTime::fromString(server.value(u"completed"_s).toString(), Qt::ISODate));
            }
        }
        QVariantMap updated = row;
        updated.insert(u"categories"_s, names);
        if (flag.status.isEmpty()) updated.remove(u"followUp"_s);
        else updated.insert(u"followUp"_s, M::FollowUps::toVariantMap(flag));
        updated.insert(u"flagged"_s, flag.isFlagged());
        if (updated == row) continue;
        changed = store->upsert(u"messages"_s, updated).value(u"ok"_s).toBool() || changed;
    }
    if (changed) static_cast<void>(m_service->saveStore());
}

void MailOrganizer::decorate(QVariantMap *row) const
{
    const QString owner = m_service->ownerAccountOf(*row);
    const QList<M::Category> list = listFor(owner);
    QVariantList chips;
    for (const QString &name : row->value(u"categories"_s).toStringList()) {
        const int at = M::Categories::indexOf(list, name);
        chips.append(QVariantMap{{u"name"_s, name}, {u"color"_s, at >= 0 ? list.at(at).color : QString()}});
    }
    const M::FollowUp flag = followOf(*row);
    row->insert(u"categoryChips"_s, chips);
    row->insert(u"followUpStatus"_s, flag.status);
    row->insert(u"followUpDue"_s, M::FollowUps::dueLabel(flag, today()));
    if (text(*row, "importance").isEmpty()) row->insert(u"importance"_s, u"normal"_s);
}

bool MailOrganizer::isFlagCard(const QString &taskId)
{
    return taskId.startsWith(kFlagCard);
}

QVariantList MailOrganizer::flagCards(const QString &status) const
{
    QList<QVariantMap> cards;
    for (const QVariant &value : m_service->store()->messages()) {
        const QVariantMap row = value.toMap();
        const M::FollowUp flag = followOf(row);
        if (flag.status.isEmpty()) continue;
        const QString column = flag.isComplete() ? u"done"_s : flag.column.isEmpty() ? u"todo"_s : flag.column;
        if (column != status) continue;
        // Outlook's To-Do leaves out Deleted Items.
        const AccountRuntime *rt = m_service->runtime(m_service->ownerAccountOf(row));
        const QString mailbox = rt != nullptr ? text(row, "remoteMailbox") : text(row, "localMailbox");
        if (mailbox == (rt != nullptr ? rt->specialMailbox(u"trash"_s) : u"Trash"_s) && !mailbox.isEmpty()) continue;
        const QString importance = text(row, "importance");
        cards.append({{u"taskId"_s, kFlagCard + text(row, "id")},
                      {u"title"_s, text(row, "subject").isEmpty() ? tr("(no subject)") : text(row, "subject")},
                      {u"assignee"_s, text(row, "sender")},
                      {u"dueDate"_s, flag.due.isValid() ? flag.due.toString(Qt::ISODate) : QString()},
                      {u"priority"_s, importance == "high"_L1 ? tr("High") : importance == "low"_L1 ? tr("Low") : tr("Normal")},
                      {u"syncState"_s, QString()},
                      {u"accountId"_s, m_service->ownerAccountOf(row)},
                      {u"isMail"_s, true},
                      {u"categories"_s, row.value(u"categories"_s).toStringList()},
                      {u"date"_s, row.value(u"date"_s)}});
    }
    std::stable_sort(cards.begin(), cards.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const QString da = a.value(u"dueDate"_s).toString(), db = b.value(u"dueDate"_s).toString();
        if (da != db) return db.isEmpty() || (!da.isEmpty() && da < db);
        return a.value(u"date"_s).toString() > b.value(u"date"_s).toString();
    });
    QVariantList out;
    for (QVariantMap &card : cards) {
        card.remove(u"date"_s);
        out.append(card);
    }
    return out;
}

void MailOrganizer::moveFlagCard(const QString &requestId, const QString &taskId, const QString &toStatus)
{
    const QString messageId = taskId.mid(kFlagCard.size());
    const QVariantMap message = record(messageId);
    if (message.isEmpty()) {
        m_service->finishNow(requestId, u"rejected"_s, tr("The message is no longer stored"));
        return;
    }
    M::FollowUp flag = followOf(message);
    const bool wasComplete = flag.isComplete();
    if (toStatus == "done"_L1) {
        flag = M::FollowUps::completed(flag, QDateTime::currentDateTimeUtc());
    } else {
        flag.status = u"flagged"_s;
        flag.completed = {};
        flag.column = toStatus == "doing"_L1 ? u"doing"_s : u"todo"_s;
    }
    QString error;
    const bool statusChanged = wasComplete != flag.isComplete();
    if (!editRecord(u"messages"_s, messageId,
                    {{u"followUp"_s, M::FollowUps::toVariantMap(flag)}, {u"flagged"_s, flag.isFlagged()},
                     {u"tagsPending"_s, statusChanged ? QVariant(true) : QVariant()}},
                    &error)) {
        m_service->finishNow(requestId, u"rejected"_s, error);
        return;
    }
    m_service->rebuildTasks();
    if (!statusChanged) {
        m_service->finishNow(requestId, u"accepted"_s, QString());
        return;
    }
    QPointer<MailOrganizer> self(this);
    pushMessageTags(messageId, false, true, [self, requestId](const QVariantMap &result) {
        if (self != nullptr)
            self->m_service->finishNow(requestId, result.value(u"state"_s).toString(), result.value(u"error"_s).toString());
    });
}

} // namespace QindaOffice::Apps::Mail
