// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: categories and follow-up flags on mail, events, tasks and
// contacts; their server mapping (IMAP keywords and \Flagged, Microsoft
// Graph, EWS); flagged mail on the task board (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "imap_extras.h"
#include "mail_service.h"
#include "provider_calls.h"

#include "contact_store.h"
#include "exchange_mailbox.h"

#include <qindatk/local_productivity_store.h>

#include <QLocale>
#include <QTimeZone>
#include <memory>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

QString text(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).toString();
}

// Several requests answered as one (the worst state wins).
struct Fold {
    int pending = 1;
    QString state = QStringLiteral("accepted");
    QString error;
    std::function<void(const QString &, const QString &)> done;
    void add(const QVariantMap &result)
    {
        const QString s = result.value(u"state"_s).toString();
        static const QStringList order{u"accepted"_s, u"offline"_s, u"conflict"_s, u"rejected"_s};
        if (order.indexOf(s) > order.indexOf(state)) {
            state = s;
            error = result.value(u"error"_s).toString();
        }
        if (--pending == 0 && done) done(state, error);
    }
};

QVariantMap flagMap(const M::FollowUp &flag)
{
    return {{u"status"_s, flag.status},
            {u"start"_s, flag.start.isValid() ? flag.start.toString(Qt::ISODate) : QString()},
            {u"due"_s, flag.due.isValid() ? flag.due.toString(Qt::ISODate) : QString()},
            {u"completed"_s, flag.completed.isValid() ? flag.completed.toUTC().toString(Qt::ISODate) : QString()}};
}

M::FollowUp followOf(const QVariantMap &record)
{
    M::FollowUp flag = M::FollowUps::fromVariantMap(record.value(u"followUp"_s).toMap());
    if (flag.status.isEmpty() && record.value(u"flagged"_s).toBool()) {
        // \Flagged from the server (or an older QindaMail): flagged, no date.
        flag.status = u"flagged"_s;
        flag.preset = u"noDate"_s;
        flag.column = u"todo"_s;
    }
    return flag;
}

} // namespace

void MailOrganizer::tellOnce(const QString &key, const QString &title, const QString &message)
{
    if (m_told.contains(key)) return;
    m_told.insert(key);
    emit m_service->notify(title, message, u"info"_s);
}

QStringList MailOrganizer::categoriesOf(const QString &kind, const QString &id) const
{
    if (kind == "contact"_L1) return m_service->contacts()->contactById(id).value(u"categories"_s).toStringList();
    const QVariantList rows = kind == "event"_L1 ? m_service->store()->events()
                            : kind == "task"_L1  ? m_service->store()->tasks()
                                                 : m_service->store()->messages();
    for (const QVariant &value : rows) {
        if (text(value.toMap(), "id") == id) return value.toMap().value(u"categories"_s).toStringList();
    }
    return {};
}

QString MailOrganizer::accountOf(const QString &kind, const QString &id) const
{
    if (kind == "message"_L1) {
        const QVariantMap message = record(id);
        return message.isEmpty() ? QString() : m_service->ownerAccountOf(message);
    }
    if (kind == "event"_L1) {
        for (const QVariant &value : m_service->store()->events())
            if (text(value.toMap(), "id") == id) return m_service->ownerAccountOf(value.toMap());
    }
    return QString::fromLatin1(kLocalAccountId);
}

QString MailOrganizer::setCategories(const QString &kind, const QStringList &ids, const QStringList &names)
{
    return applyCategories(kind, ids, [names](const QStringList &) { return names; });
}

QString MailOrganizer::toggleCategory(const QString &kind, const QStringList &ids, const QString &name, bool on)
{
    return applyCategories(kind, ids, [name, on](const QStringList &current) {
        QStringList names = current;
        names.removeIf([&name](const QString &n) { return n.compare(name, Qt::CaseInsensitive) == 0; });
        if (on) names.append(name);
        return names;
    });
}

QString MailOrganizer::applyCategories(const QString &kind, const QStringList &ids,
                                       const std::function<QStringList(const QStringList &)> &change)
{
    const QString requestId = ProviderCalls::newRequestId(u"tags"_s);
    auto fold = std::make_shared<Fold>();
    QPointer<MailOrganizer> self(this);
    fold->done = [self, requestId](const QString &state, const QString &error) {
        if (self != nullptr) self->answerLater("tags", requestId, ProviderCalls::result({}, state, error));
    };
    const auto one = [fold](const QVariantMap &result) { fold->add(result); };
    for (const QString &id : ids) {
        const QStringList current = categoriesOf(kind, id);
        const QStringList names = M::Categories::normalized(change(current));
        ++fold->pending;
        if (names == current) {
            one(ProviderCalls::result({}, u"accepted"_s));
            continue;
        }
        QString error;
        if (kind == "contact"_L1) {
            QVariantMap contact = m_service->contacts()->contactById(id);
            contact.insert(u"categories"_s, names);
            const QVariantMap updated = m_service->contacts()->update(contact);
            one(ProviderCalls::result({}, updated.value(u"ok"_s).toBool() ? u"accepted"_s : u"rejected"_s,
                                      updated.value(u"error"_s).toString()));
        } else if (kind == "task"_L1) {
            const bool ok = editRecord(u"tasks"_s, id, {{u"categories"_s, names}}, &error);
            if (ok) m_service->rebuildTasks();
            one(ProviderCalls::result({}, ok ? u"accepted"_s : u"rejected"_s, error));
        } else if (kind == "event"_L1) {
            if (!editRecord(u"events"_s, id, {{u"categories"_s, names}}, &error)) one(ProviderCalls::result({}, u"rejected"_s, error));
            else pushEventCategories(id, one);
        } else {
            if (!editRecord(u"messages"_s, id, {{u"categories"_s, names}, {u"tagsPending"_s, true}}, &error))
                one(ProviderCalls::result({}, u"rejected"_s, error));
            else pushMessageTags(id, true, false, one);
        }
    }
    fold->add(ProviderCalls::result({}, u"accepted"_s));     // release the guard
    return requestId;
}

QString MailOrganizer::followUp(const QStringList &messageIds, const QString &preset)
{
    const QDate day = today();
    const QDateTime now = m_today.isValid() ? QDateTime(m_today, QTime(12, 0)) : QDateTime::currentDateTimeUtc();
    return applyFollowUp(messageIds, [preset, day, now](const QVariantMap &message) {
        if (preset == "clear"_L1) return M::FollowUp();
        if (preset == "complete"_L1) return M::FollowUps::completed(followOf(message), now);
        return M::FollowUps::forPreset(preset, day, QLocale().firstDayOfWeek());
    });
}

QString MailOrganizer::followUpCustom(const QStringList &messageIds, const QString &start, const QString &due)
{
    const M::FollowUp flag = M::FollowUps::custom(QDate::fromString(start, Qt::ISODate), QDate::fromString(due, Qt::ISODate));
    return applyFollowUp(messageIds, [flag](const QVariantMap &) { return flag; });
}

QString MailOrganizer::applyFollowUp(const QStringList &ids, const std::function<M::FollowUp(const QVariantMap &)> &make)
{
    const QString requestId = ProviderCalls::newRequestId(u"flag"_s);
    auto fold = std::make_shared<Fold>();
    QPointer<MailOrganizer> self(this);
    fold->done = [self, requestId](const QString &state, const QString &error) {
        if (self != nullptr) self->answerLater("tags", requestId, ProviderCalls::result({}, state, error));
    };
    for (const QString &id : ids) {
        const QVariantMap message = record(id);
        if (message.isEmpty()) continue;
        const M::FollowUp flag = make(message);
        ++fold->pending;
        QString error;
        const QVariantMap changes{{u"followUp"_s, flag.status.isEmpty() ? QVariant() : QVariant(M::FollowUps::toVariantMap(flag))},
                                  {u"flagged"_s, flag.isFlagged()}, {u"tagsPending"_s, true}};
        if (!editRecord(u"messages"_s, id, changes, &error)) {
            fold->add(ProviderCalls::result({}, u"rejected"_s, error));
            continue;
        }
        pushMessageTags(id, false, true, [fold](const QVariantMap &result) { fold->add(result); });
    }
    fold->add(ProviderCalls::result({}, u"accepted"_s));
    m_service->rebuildTasks();
    return requestId;
}

QVariantMap MailOrganizer::followUpOf(const QString &messageId) const
{
    const M::FollowUp flag = followOf(record(messageId));
    QVariantMap out = flagMap(flag);
    out.insert(u"preset"_s, flag.preset);
    out.insert(u"text"_s, M::FollowUps::describe(flag));
    out.insert(u"dueLabel"_s, M::FollowUps::dueLabel(flag, today()));
    return out;
}

void MailOrganizer::pushMessageTags(const QString &messageId, bool categories, bool flag, Done done)
{
    const QVariantMap message = record(messageId);
    const QString owner = m_service->ownerAccountOf(message);
    AccountRuntime *rt = m_service->runtime(owner);
    if (message.isEmpty() || rt == nullptr) {
        // This computer's mail: the local edit is all there is.
        if (!message.isEmpty()) static_cast<void>(editRecord(u"messages"_s, messageId, {{u"tagsPending"_s, QVariant()}}));
        done(ProviderCalls::result({}, u"accepted"_s));
        return;
    }
    QPointer<MailOrganizer> self(this);
    auto fold = std::make_shared<Fold>();
    auto settled = std::make_shared<bool>(true);   // every server that should know, knows
    fold->done = [self, messageId, settled, done](const QString &state, const QString &error) {
        if (self != nullptr && *settled) static_cast<void>(self->editRecord(u"messages"_s, messageId, {{u"tagsPending"_s, QVariant()}}));
        done(ProviderCalls::result({}, state, error));
    };
    const QStringList names = message.value(u"categories"_s).toStringList();
    if (flag && (rt->isImap() || rt->isPop3())) {
        // \Flagged (IMAP), or POP3's local mailbox; Exchange maps \Flagged too.
        ++fold->pending;
        const QString batch = m_service->beginBatch(u"flag"_s, 1);
        expect(batch, [fold](const QVariantMap &result) { fold->add(result); });
        m_service->runMessageAction(batch, messageId, message.value(u"flagged"_s).toBool() ? u"flag"_s : u"unflag"_s);
    }
    QindaOffice::Mail::ExchangeMailbox *box = rt->mailbox();
    if (categories && rt->isImap() && box == nullptr && !m_keywordSupport.value(owner, true)) {
        tellOnce(u"keywords\n"_s + owner, tr("Categories stay on this computer"),
                 tr("%1's mail server keeps no keywords, so its messages' categories are kept by QindaMail.")
                     .arg(m_service->accountName(owner)));
    } else if (categories && rt->isImap() && box == nullptr) {
        const QStringList remote = message.value(u"remoteCategories"_s).toStringList();
        QStringList add, remove;
        for (const QString &name : names)
            if (!remote.contains(name, Qt::CaseInsensitive)) add.append(M::Categories::keywordFor(name));
        for (const QString &name : remote)
            if (!names.contains(name, Qt::CaseInsensitive)) remove.append(M::Categories::keywordFor(name));
        if (!add.isEmpty() || !remove.isEmpty()) {
            ++fold->pending;
            ImapExtras *extras = rt->imapExtras();
            const QString mailbox = text(message, "remoteMailbox");
            const qint64 validity = message.value(u"remoteUidValidity"_s).toLongLong();
            const qint64 uid = message.value(u"remoteUid"_s).toLongLong();
            rt->run(u"imap"_s, extras,
                    [extras, mailbox, validity, uid, add, remove](const QString &rid) {
                        return extras->storeKeywords(rid, mailbox, validity, uid, add, remove);
                    },
                    [self, fold, settled, owner, messageId, names](const QVariantMap &result) {
                        const QString state = result.value(u"state"_s).toString();
                        if (self != nullptr && state == "accepted"_L1) {
                            static_cast<void>(self->editRecord(u"messages"_s, messageId, {{u"remoteCategories"_s, names}}));
                        } else if (self != nullptr && state == "rejected"_L1) {
                            // The server takes no new keywords: categories stay here.
                            self->m_keywordSupport.insert(owner, false);
                            self->save();
                            self->tellOnce(u"keywords\n"_s + owner, tr("Categories stay on this computer"),
                                           tr("%1's mail server keeps no keywords, so its messages' categories are kept by QindaMail.")
                                               .arg(self->m_service->accountName(owner)));
                        } else {
                            *settled = false;                       // offline: try again after the next sync
                        }
                        fold->add(ProviderCalls::result({}, u"accepted"_s));
                    },
                    false);
        }
    }
    if ((categories || flag) && box != nullptr && !m_mailboxBlocked.contains(owner)) {
        ++fold->pending;
        QVariantMap item{{u"kind"_s, u"message"_s}, {u"internetMessageId"_s, text(message, "internetMessageId")},
                         {u"folder"_s, text(message, "remoteMailbox")},
                         {u"zone"_s, QString::fromLatin1(QTimeZone::systemTimeZoneId())}};
        if (categories) item.insert(u"categories"_s, names);
        if (flag) item.insert(u"flag"_s, flagMap(followOf(message)));
        const QString service = rt->mailboxService() == "graph"_L1 ? u"graphmail"_s : u"ews"_s;
        rt->run(service, box, [box, item](const QString &rid) { return box->setItemState(rid, item); },
                [self, fold, settled, owner](const QVariantMap &result) {
                    if (!result.value(u"ok"_s).toBool()) {
                        *settled = false;
                        if (self != nullptr && (result.value(u"consentRequired"_s).toBool() || result.value(u"needsSignIn"_s).toBool())) {
                            self->m_mailboxBlocked.insert(owner);
                            self->tellOnce(u"mailbox\n"_s + owner, tr("Sign in again to keep tags in Outlook"),
                                           tr("Categories and follow-up dates of %1 stay on this computer until you sign in again "
                                              "(Accounts ▸ Account Settings).").arg(self->m_service->accountName(owner)));
                        }
                    }
                    fold->add(ProviderCalls::result({}, u"accepted"_s));
                },
                false);
    }
    fold->add(ProviderCalls::result({}, u"accepted"_s));
}

void MailOrganizer::pushEventCategories(const QString &eventId, Done done)
{
    QVariantMap event;
    for (const QVariant &value : m_service->store()->events())
        if (text(value.toMap(), "id") == eventId) event = value.toMap();
    AccountRuntime *rt = m_service->runtime(text(event, "remoteAccount"));
    const CalendarBinding *binding = rt != nullptr ? rt->calendar(text(event, "calendarId")) : nullptr;
    QindaOffice::Mail::ExchangeMailbox *box = rt != nullptr ? rt->mailbox() : nullptr;
    if (binding == nullptr || box == nullptr || (binding->service != "graph"_L1 && binding->service != "ews"_L1)) {
        // CalDAV, a feed or this computer: kept here (QindaTK's VEVENT
        // mapping has no CATEGORIES yet, D-090).
        done(ProviderCalls::result({}, u"accepted"_s));
        return;
    }
    // Marked until the server has them: the calendar sync keeps them (and
    // noteCalendarSync sends them once a new event has its server id).
    static_cast<void>(editRecord(u"events"_s, eventId, {{u"categoriesPending"_s, true}}));
    if (text(event, "remoteId").isEmpty()) {
        done(ProviderCalls::result({}, u"accepted"_s));
        return;
    }
    const QVariantMap item{{u"kind"_s, u"event"_s}, {u"remoteId"_s, text(event, "remoteId")},
                           {u"categories"_s, event.value(u"categories"_s).toStringList()}};
    QPointer<MailOrganizer> self(this);
    // The calendar's own credential: Graph's calendar token, or EWS's.
    rt->run(binding->service, box, [box, item](const QString &rid) { return box->setItemState(rid, item); },
            [self, eventId, done](const QVariantMap &result) {
                if (self != nullptr && result.value(u"ok"_s).toBool())
                    static_cast<void>(self->editRecord(u"events"_s, eventId, {{u"categoriesPending"_s, QVariant()}}));
                done(result);
            },
            false);
}

void MailOrganizer::noteCalendarSync(AccountRuntime *rt)
{
    if (rt == nullptr || rt->mailbox() == nullptr) return;
    int sent = 0;
    for (const QVariant &value : m_service->store()->events()) {
        const QVariantMap event = value.toMap();
        if (text(event, "remoteAccount") != rt->id() || !event.value(u"categoriesPending"_s).toBool()
            || text(event, "remoteId").isEmpty() || sent++ >= 20)
            continue;
        pushEventCategories(text(event, "id"), [](const QVariantMap &) {});
    }
}

} // namespace QindaOffice::Apps::Mail
