// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: its file, message facts and the category master lists
// (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "mail_service.h"
#include "provider_calls.h"

#include "exchange_mailbox.h"
#include "json_settings.h"

#include <qindatk/local_productivity_store.h>

#include <QLocale>
#include <QMetaObject>
#include <QTimer>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

constexpr const char kSchema[] = "qindaoffice.mail.organizer/1";

QString text(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).toString();
}

} // namespace

MailOrganizer::MailOrganizer(MailService *service, const QString &path, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_file(std::make_unique<Core::JsonSettings>(path))
{
    load();
    // Requests this object started through the service (mark read, delete,
    // move …) answer through its operationFinished; their continuation runs here.
    connect(service, &MailService::operationFinished, this,
            [this](const QString &requestId, const QString &state, const QString &message) {
                const Done done = m_waiting.take(requestId);
                if (done) done(ProviderCalls::result(requestId, state, message));
            });
}

MailOrganizer::~MailOrganizer() = default;

void MailOrganizer::load()
{
    static_cast<void>(m_file->load());
    const QVariantMap root = m_file->valueMap();
    m_rules = M::Rules::listFromVariant(root.value(u"rules"_s));
    m_searchFolders = M::SearchFolders::listFromVariant(root.value(u"searchFolders"_s));
    const QVariantMap lists = root.value(u"categories"_s).toMap();
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) m_categories.insert(it.key(), M::Categories::listFromVariant(it.value()));
    // AGENT-NOTE: a first run starts with Outlook's six color categories.
    if (!m_categories.contains(u"local"_s)) m_categories.insert(u"local"_s, M::Categories::defaults());
    for (const QString &key : root.value(u"baselines"_s).toStringList()) m_baselines.insert(key);
    m_ignored = root.value(u"ignoredThreads"_s).toStringList();
    const QVariantMap keywords = root.value(u"keywordSupport"_s).toMap();
    for (auto it = keywords.cbegin(); it != keywords.cend(); ++it) m_keywordSupport.insert(it.key(), it.value().toBool());
}

void MailOrganizer::save()
{
    m_file->setValue(u"schema"_s, QString::fromLatin1(kSchema));
    m_file->setValue(u"rules"_s, M::Rules::listToVariant(m_rules));
    m_file->setValue(u"searchFolders"_s, M::SearchFolders::listToVariant(m_searchFolders));
    QVariantMap lists;
    for (auto it = m_categories.cbegin(); it != m_categories.cend(); ++it) lists.insert(it.key(), M::Categories::listToVariant(it.value()));
    m_file->setValue(u"categories"_s, lists);
    QStringList baselines(m_baselines.cbegin(), m_baselines.cend());
    baselines.sort();
    m_file->setValue(u"baselines"_s, baselines);
    m_file->setValue(u"ignoredThreads"_s, m_ignored);
    QVariantMap keywords;
    for (auto it = m_keywordSupport.cbegin(); it != m_keywordSupport.cend(); ++it) keywords.insert(it.key(), it.value());
    m_file->setValue(u"keywordSupport"_s, keywords);
    if (!m_file->save()) emit m_service->notify(tr("Rules and categories were not saved"), m_file->path(), u"danger"_s);
}

void MailOrganizer::bump()
{
    ++m_revision;
    emit changed();
    if (m_service != nullptr) m_service->scheduleRefresh();
}

QDate MailOrganizer::today() const
{
    return m_today.isValid() ? m_today : QDate::currentDate();
}

M::CriteriaContext MailOrganizer::context() const
{
    M::CriteriaContext c;
    c.myAddresses = m_service->ownAddresses();
    c.now = m_today.isValid() ? QDateTime(m_today, QTime(12, 0)).toUTC() : QDateTime::currentDateTimeUtc();
    return c;
}

QVariantMap MailOrganizer::record(const QString &messageId) const
{
    return m_service->rawMessage(messageId);
}

M::MessageFacts MailOrganizer::factsOf(const QVariantMap &r, const QString &headers) const
{
    M::MessageFacts f;
    f.id = text(r, "id");
    f.accountId = m_service->ownerAccountOf(r);
    f.folderKey = m_service->folderKeyOf(r);
    f.senderName = text(r, "sender");
    f.senderAddress = text(r, "senderAddress");
    f.to = text(r, "to");
    f.cc = text(r, "cc");
    f.subject = text(r, "subject");
    f.body = text(r, "bodyPlain").isEmpty() ? text(r, "snippet") : text(r, "bodyPlain");
    f.headers = headers;
    f.headersKnown = !headers.isEmpty();
    f.hasAttachments = !r.value(u"attachments"_s).toList().isEmpty();
    f.importance = text(r, "importance");
    if (f.importance.isEmpty()) f.importance = f.headersKnown ? M::Criteria::importanceFromHeaders(headers) : u"normal"_s;
    // AGENT-NOTE: no file is read here — search folders evaluate every
    // message on each refresh. Without the server's size, the body and the
    // attachments stand in for it.
    f.sizeBytes = r.value(u"remoteSize"_s).toLongLong();
    if (f.sizeBytes <= 0) {
        f.sizeBytes = f.body.toUtf8().size();
        for (const QVariant &a : r.value(u"attachments"_s).toList()) f.sizeBytes += a.toMap().value(u"size"_s).toLongLong();
    }
    f.received = QDateTime::fromString(text(r, "date"), Qt::ISODate);
    f.categories = r.value(u"categories"_s).toStringList();
    const M::FollowUp flag = M::FollowUps::fromVariantMap(r.value(u"followUp"_s).toMap());
    f.flagged = flag.isFlagged() || (flag.status.isEmpty() && r.value(u"flagged"_s).toBool());
    f.unread = r.value(u"unread"_s).toBool();
    return f;
}

bool MailOrganizer::editRecord(const QString &collection, const QString &id, const QVariantMap &changes, QString *error)
{
    // M89: the store's id index finds the record (QindaTK D-324), not a scan.
    QindaTK::LocalProductivityStore *store = m_service->store();
    QVariantMap row = store->record(collection, id);
    if (row.isEmpty()) {
        if (error != nullptr) *error = tr("The item no longer exists");
        return false;
    }
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (it.value().isNull()) row.remove(it.key());
        else row.insert(it.key(), it.value());
    }
    const QVariantMap upserted = store->upsert(collection, row);
    if (!upserted.value(u"ok"_s).toBool()) {
        if (error != nullptr) *error = upserted.value(u"error"_s).toString();
        return false;
    }
    return m_service->saveStore(error);
}

void MailOrganizer::answerLater(const char *signal, const QString &requestId, const QVariantMap &result)
{
    QVariantMap out = result;
    out.insert(u"requestId"_s, requestId);
    const QByteArray name(signal);
    QTimer::singleShot(0, this, [this, name, requestId, out] {
        if (name == "rules") emit rulesRunFinished(requestId, out);
        else if (name == "tags") emit tagsFinished(requestId, out);
        else emit autoRepliesFinished(requestId, out);
    });
}

void MailOrganizer::expect(const QString &requestId, Done done)
{
    m_waiting.insert(requestId, std::move(done));
}

// ---- category master lists ----

QString MailOrganizer::listKeyFor(const QString &accountId) const
{
    const AccountRuntime *rt = m_service->runtime(accountId);
    return rt != nullptr && !rt->mailboxService().isEmpty() ? accountId : u"local"_s;
}

QList<M::Category> &MailOrganizer::listFor(const QString &accountId)
{
    const QString key = listKeyFor(accountId);
    auto it = m_categories.find(key);
    if (it == m_categories.end()) it = m_categories.insert(key, M::Categories::defaults());
    return *it;
}

const QList<M::Category> MailOrganizer::listFor(const QString &accountId) const
{
    const QString key = listKeyFor(accountId);
    return m_categories.contains(key) ? m_categories.value(key) : M::Categories::defaults();
}

QVariantList MailOrganizer::categoriesFor(const QString &accountId) const
{
    QVariantList out;
    for (const M::Category &category : listFor(accountId)) {
        QVariantMap map = M::Categories::toVariantMap(category);
        map.insert(u"serverOwned"_s, listKeyFor(accountId) != "local"_L1);
        out.append(map);
    }
    return out;
}

QString MailOrganizer::categoryColor(const QString &accountId, const QString &name) const
{
    const QList<M::Category> list = listFor(accountId);
    const int at = M::Categories::indexOf(list, name);
    return at >= 0 ? list.at(at).color : QString();
}

QVariantList MailOrganizer::categoryPalette() const
{
    QVariantList out;
    for (int i = 0; i < M::Categories::presets().size(); ++i)
        out.append(QVariantMap{{u"name"_s, QString::fromLatin1(M::Categories::presets().at(i).name)},
                               {u"color"_s, M::Categories::colorForPreset(i)}, {u"preset"_s, i}});
    return out;
}

QVariantMap MailOrganizer::addCategory(const QString &accountId, const QString &name, const QString &color)
{
    M::Category category;
    category.name = name.trimmed();
    category.color = color.trimmed().toLower();
    category.preset = M::Categories::presetForColor(category.color);
    if (category.color.isEmpty()) {
        category.preset = int(listFor(accountId).size() % M::Categories::presets().size());
        category.color = M::Categories::colorForPreset(category.preset);
    }
    QList<M::Category> &list = listFor(accountId);
    const QStringList problems = M::Categories::validate(category, list);
    if (!problems.isEmpty()) return {{u"ok"_s, false}, {u"error"_s, problems.first().section(u": "_s, 1)}};
    if (list.size() >= M::Categories::kMaxCategories) return {{u"ok"_s, false}, {u"error"_s, tr("The list is full")}};
    list.append(category);
    save();
    bump();
    AccountRuntime *rt = m_service->runtime(accountId);
    if (rt != nullptr && rt->mailbox() != nullptr && rt->mailboxService() == "graph"_L1) {
        // Microsoft 365 keeps the master list: add it there too.
        QindaOffice::Mail::ExchangeMailbox *box = rt->mailbox();
        rt->run(u"graphmail"_s, box,
                [box, category](const QString &rid) { return box->addCategory(rid, category.name, category.preset); },
                [](const QVariantMap &) {}, false);
    }
    return {{u"ok"_s, true}, {u"error"_s, QString()}};
}

QVariantMap MailOrganizer::updateCategory(const QString &accountId, const QString &name, const QVariantMap &changes)
{
    if (listKeyFor(accountId) != "local"_L1)
        return {{u"ok"_s, false},
                {u"error"_s, tr("Exchange keeps this account's categories; change them in Outlook on the web")}};
    QList<M::Category> &list = listFor(accountId);
    const int at = M::Categories::indexOf(list, name);
    if (at < 0) return {{u"ok"_s, false}, {u"error"_s, tr("There is no category “%1”").arg(name)}};
    M::Category updated = list.at(at);
    if (changes.contains(u"color"_s)) {
        updated.color = changes.value(u"color"_s).toString().trimmed().toLower();
        updated.preset = M::Categories::presetForColor(updated.color);
    }
    const QString newName = changes.value(u"name"_s, name).toString().trimmed();
    QList<M::Category> others = list;
    others.removeAt(at);
    updated.name = newName;
    const QStringList problems = M::Categories::validate(updated, others);
    if (!problems.isEmpty()) return {{u"ok"_s, false}, {u"error"_s, problems.first().section(u": "_s, 1)}};
    list[at] = updated;
    if (newName != name) {
        // A renamed category is renamed on every item that uses this list.
        const auto renamed = [&name, &newName](QStringList names) {
            bool hit = false;
            for (QString &n : names) {
                if (n.compare(name, Qt::CaseInsensitive) == 0) {
                    n = newName;
                    hit = true;
                }
            }
            return hit ? names : QStringList();
        };
        QindaTK::LocalProductivityStore *store = m_service->store();
        const QList<QPair<QString, QVariantList>> collections{
            {u"messages"_s, store->messages()}, {u"events"_s, store->events()}, {u"tasks"_s, store->tasks()}};
        for (const auto &[collection, rows] : collections) {
            QVariantList batch; // M89: one store change per collection (upsertMany)
            for (const QVariant &value : rows) {
                QVariantMap row = value.toMap();
                const QStringList names = renamed(row.value(u"categories"_s).toStringList());
                if (names.isEmpty()) continue;
                if (collection == "messages"_L1 && listKeyFor(m_service->ownerAccountOf(row)) != "local"_L1) continue;
                row.insert(u"categories"_s, names);
                batch.append(row);
            }
            if (!batch.isEmpty()) static_cast<void>(store->upsertMany(collection, batch));
        }
        static_cast<void>(m_service->saveStore());
        for (const QVariant &value : m_service->contacts()->search(QString(), 100000)) {
            QVariantMap contact = value.toMap();
            const QStringList names = renamed(contact.value(u"categories"_s).toStringList());
            if (names.isEmpty()) continue;
            contact.insert(u"categories"_s, names);
            static_cast<void>(m_service->contacts()->update(contact));
        }
    }
    save();
    bump();
    return {{u"ok"_s, true}, {u"error"_s, QString()}};
}

bool MailOrganizer::deleteCategory(const QString &accountId, const QString &name)
{
    if (listKeyFor(accountId) != "local"_L1) return false;
    QList<M::Category> &list = listFor(accountId);
    const int at = M::Categories::indexOf(list, name);
    if (at < 0) return false;
    // As in Outlook, items keep the name; it is just no longer in the list.
    list.removeAt(at);
    save();
    bump();
    return true;
}

void MailOrganizer::syncCategoryList(AccountRuntime *rt)
{
    QindaOffice::Mail::ExchangeMailbox *box = rt != nullptr ? rt->mailbox() : nullptr;
    if (box == nullptr) return;
    const QString accountId = rt->id();
    const QString service = rt->mailboxService() == "graph"_L1 ? u"graphmail"_s : u"ews"_s;
    QPointer<MailOrganizer> self(this);
    rt->run(service, box, [box](const QString &rid) { return box->listCategories(rid); },
            [self, accountId](const QVariantMap &result) {
                if (self == nullptr || !result.value(u"ok"_s).toBool()) return;
                const QVariantList server = result.value(u"categories"_s).toList();
                if (server.isEmpty()) return;          // never customised: Outlook's defaults stand
                QList<M::Category> list;
                for (const QVariant &value : server) {
                    const QVariantMap row = value.toMap();
                    M::Category category;
                    category.name = text(row, "name").trimmed();
                    category.preset = row.value(u"preset"_s).toInt();
                    category.color = category.preset >= 0 ? M::Categories::colorForPreset(category.preset) : u"#a19f9d"_s;
                    category.extra.insert(u"serverId"_s, row.value(u"id"_s));
                    if (!category.name.isEmpty() && M::Categories::indexOf(list, category.name) < 0) list.append(category);
                }
                if (M::Categories::listToVariant(list) == M::Categories::listToVariant(self->m_categories.value(accountId))) return;
                self->m_categories.insert(accountId, list);
                self->save();
                self->bump();
            },
            false);
}

} // namespace QindaOffice::Apps::Mail
