// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: the folder tree, the current folder's message list and the
// selection (mail_service.h; keys in folder_model.h).
#include "mail_service.h"

#include "account_store.h"
#include "mail_organizer.h"
#include "message_list_model.h"

#include <qindatk/local_productivity_store.h>

#include <QTimer>
#include <algorithm>

namespace QindaOffice::Apps::Mail {

namespace {

const QString kAllInbox = QStringLiteral("all:inbox");

QString accountKey(const QString &accountId)
{
    return QStringLiteral("acct:") + accountId;
}

QString folderKey(const QString &accountId, const QString &mailbox)
{
    return accountKey(accountId) + QLatin1Char(':') + mailbox;
}

// Special folders first (in this order), ordinary folders in server order,
// junk and trash last — the order people expect in a folder pane.
int roleRank(const QString &role)
{
    static const QStringList order{QStringLiteral("inbox"), QStringLiteral("drafts"), QStringLiteral("outbox"),
                                   QStringLiteral("sent"), QStringLiteral("archive"), QStringLiteral("flagged"),
                                   QStringLiteral("all")};
    if (role == QLatin1String("junk")) return 20;
    if (role == QLatin1String("trash")) return 21;
    const qsizetype at = order.indexOf(role);
    return at < 0 ? 10 : int(at);
}

struct Mailbox {
    QString name;
    QString delimiter;
    QString specialUse;
    bool selectable = true;
    int order = 0;
};

QString parentOf(const Mailbox &mailbox)
{
    if (mailbox.delimiter.isEmpty()) return {};
    const qsizetype at = mailbox.name.lastIndexOf(mailbox.delimiter);
    return at <= 0 ? QString() : mailbox.name.left(at);
}

} // namespace

QString MailService::ownerAccountOf(const QVariantMap &record) const
{
    const QString remote = record.value(QStringLiteral("remoteAccount")).toString();
    if (!remote.isEmpty()) return remote;
    const QString account = record.value(QStringLiteral("accountId")).toString();
    return account.isEmpty() ? QString::fromLatin1(kLocalAccountId) : account;
}

QString MailService::folderKeyOf(const QVariantMap &record) const
{
    const QString remote = record.value(QStringLiteral("remoteAccount")).toString();
    if (!remote.isEmpty() && remote != QLatin1String(kLocalAccountId)) {
        if (record.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("pop3:"))) {
            const QString local = record.value(QStringLiteral("localMailbox")).toString();
            return folderKey(remote, local.isEmpty() ? QStringLiteral("INBOX") : local);
        }
        const QString mailbox = record.value(QStringLiteral("remoteMailbox")).toString();
        return folderKey(remote, mailbox.isEmpty() ? QStringLiteral("INBOX") : mailbox);
    }
    const QString local = record.value(QStringLiteral("localMailbox")).toString();
    return folderKey(QString::fromLatin1(kLocalAccountId), local.isEmpty() ? QStringLiteral("Inbox") : local);
}

QString MailService::inboxKey(const QString &accountId) const
{
    return folderKey(accountId, accountId == QLatin1String(kLocalAccountId) ? QStringLiteral("Inbox")
                                                                            : QStringLiteral("INBOX"));
}

QHash<QString, QPair<int, int>> MailService::folderCounts() const
{
    QHash<QString, QPair<int, int>> counts;
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        QPair<int, int> &slot = counts[folderKeyOf(record)];
        slot.first += record.value(QStringLiteral("unread")).toBool() ? 1 : 0;
        slot.second += 1;
    }
    for (const QVariant &value : m_store->drafts()) {
        const bool queued = value.toMap().value(QStringLiteral("queued")).toBool();
        counts[folderKey(QString::fromLatin1(kLocalAccountId),
                         queued ? QStringLiteral("Outbox") : QStringLiteral("Drafts"))].second += 1;
    }
    return counts;
}

QVector<FolderRow> MailService::accountFolderRows(const QString &accountId,
                                                  const QHash<QString, QPair<int, int>> &counts) const
{
    QList<Mailbox> boxes;
    const auto add = [&boxes](const QString &name, const QString &role, const QString &delimiter = {},
                              bool selectable = true) {
        if (name.isEmpty()) return;
        for (const Mailbox &existing : std::as_const(boxes)) {
            if (existing.name == name) return;
        }
        boxes.append({name, delimiter, role, selectable, int(boxes.size())});
    };
    const AccountRuntime *rt = runtime(accountId);
    if (accountId == QLatin1String(kLocalAccountId)) {
        add(QStringLiteral("Inbox"), QStringLiteral("inbox"));
        add(QStringLiteral("Drafts"), QStringLiteral("drafts"));
        add(QStringLiteral("Outbox"), QStringLiteral("outbox"));
        add(QStringLiteral("Sent"), QStringLiteral("sent"));
        add(QStringLiteral("Archive"), QStringLiteral("archive"));
        add(QStringLiteral("Trash"), QStringLiteral("trash"));
        // M64: the folders imported mbox files made (add() skips the fixed
        // ones); M76: Outlook imports nest them with '/'.
        for (const QString &name : localMailboxes()) add(name, QString(), QStringLiteral("/"));
    } else if (rt != nullptr && rt->isPop3()) {
        add(QStringLiteral("INBOX"), QStringLiteral("inbox"));
        add(QStringLiteral("Archive"), QStringLiteral("archive"));
    } else if (rt != nullptr && rt->isImap()) {
        add(QStringLiteral("INBOX"), QStringLiteral("inbox"));
        for (const QVariant &value : rt->mailboxes()) {
            const QVariantMap mailbox = value.toMap();
            const QString name = mailbox.value(QStringLiteral("name")).toString();
            if (name.compare(QStringLiteral("INBOX"), Qt::CaseInsensitive) == 0) continue;
            add(name, mailbox.value(QStringLiteral("specialUse")).toString(),
                mailbox.value(QStringLiteral("delimiter")).toString(),
                mailbox.value(QStringLiteral("selectable"), true).toBool());
        }
        for (const QString &role : {QStringLiteral("sent"), QStringLiteral("drafts"), QStringLiteral("archive"),
                                    QStringLiteral("junk"), QStringLiteral("trash")}) {
            add(rt->specialMailbox(role), role);
        }
    } else {
        return {};
    }
    // Mailboxes that only the records know (a folder synced before a LIST).
    const QString prefix = accountKey(accountId) + QLatin1Char(':');
    QStringList known;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.key().startsWith(prefix)) known.append(it.key().mid(prefix.size()));
    }
    std::sort(known.begin(), known.end());
    for (const QString &name : std::as_const(known)) add(name, QString());

    // Children follow their parent; siblings by role rank, then server order.
    QHash<QString, QList<Mailbox>> children;
    QSet<QString> names;
    for (const Mailbox &box : std::as_const(boxes)) names.insert(box.name);
    for (const Mailbox &box : std::as_const(boxes)) {
        const QString parent = parentOf(box);
        children[names.contains(parent) ? parent : QString()].append(box);
    }
    for (auto it = children.begin(); it != children.end(); ++it) {
        std::stable_sort(it->begin(), it->end(), [](const Mailbox &a, const Mailbox &b) {
            const int ra = roleRank(a.specialUse), rb = roleRank(b.specialUse);
            return ra != rb ? ra < rb : a.order < b.order;
        });
    }
    QVector<FolderRow> rows;
    std::function<void(const QString &, int)> emitLevel = [&](const QString &parent, int depth) {
        for (const Mailbox &box : children.value(parent)) {
            FolderRow row;
            row.key = folderKey(accountId, box.name);
            row.accountId = accountId;
            row.path = box.name;
            const QString leaf = box.delimiter.isEmpty() ? box.name : box.name.section(box.delimiter, -1);
            row.name = box.specialUse == QLatin1String("inbox") ? tr("Inbox") : leaf;
            if (accountId == QLatin1String(kLocalAccountId)) {
                // The local folders are ours to name: translate them.
                row.name = box.name == QLatin1String("Drafts") ? tr("Drafts")
                    : box.name == QLatin1String("Outbox")      ? tr("Outbox")
                    : box.name == QLatin1String("Sent")        ? tr("Sent")
                    : box.name == QLatin1String("Archive")     ? tr("Archive")
                    : box.name == QLatin1String("Trash")       ? tr("Trash")
                                                               : row.name;
            } else if (box.name == QLatin1String("Archive") && rt != nullptr && rt->isPop3()) {
                row.name = tr("Archive");
            }
            row.depth = depth;
            row.specialUse = box.specialUse;
            row.iconName = FolderModel::iconFor(box.specialUse);
            row.unread = counts.value(row.key).first;
            row.total = counts.value(row.key).second;
            row.selectable = box.selectable;
            rows.append(row);
            emitLevel(box.name, depth + 1);
        }
    };
    emitLevel(QString(), 1);
    return rows;
}

void MailService::rebuildFolders()
{
    const auto counts = folderCounts();
    QStringList accountIds;
    for (const QindaOffice::Mail::MailAccount &account : m_accountStore->list()) {
        const AccountRuntime *rt = runtime(account.id);
        if (rt != nullptr && (rt->isImap() || rt->isPop3())) accountIds.append(account.id);
    }
    accountIds.append(QString::fromLatin1(kLocalAccountId));

    QVector<FolderRow> rows;
    FolderRow all;
    all.key = kAllInbox;
    all.name = tr("All inboxes");
    all.specialUse = QStringLiteral("inbox");
    all.iconName = QStringLiteral("inbox");
    int unreadTotal = 0;
    for (const QString &id : std::as_const(accountIds)) {
        all.unread += counts.value(inboxKey(id)).first;
        all.total += counts.value(inboxKey(id)).second;
    }
    unreadTotal = all.unread;
    rows.append(all);
    for (const QString &id : std::as_const(accountIds)) {
        FolderRow header;
        header.key = accountKey(id);
        header.accountId = id;
        header.name = accountName(id);
        header.isAccount = true;
        header.selectable = false;
        header.iconName = id == QLatin1String(kLocalAccountId) ? QStringLiteral("monitor") : QStringLiteral("at-sign");
        header.expanded = !m_collapsed.contains(header.key);
        header.unread = counts.value(inboxKey(id)).first;
        rows.append(header);
        if (header.expanded) rows += accountFolderRows(id, counts);
    }
    // M81: the saved searches, under their own header, kept live.
    rows += m_organizer->searchRows(!m_collapsed.contains(QStringLiteral("searches")));
    m_folderModel->setRows(rows);
    if (unreadTotal != m_unreadTotal) {
        m_unreadTotal = unreadTotal;
        emit unreadTotalChanged();
    }
    // A folder that went away (account removed) falls back to the first inbox.
    const bool known = !m_currentFolderKey.isEmpty()
        && (m_folderModel->indexOfKey(m_currentFolderKey) >= 0 || m_organizer->isSearchKey(m_currentFolderKey)
            || accountFolderRows(m_currentFolderKey.section(QLatin1Char(':'), 1, 1), counts).size() > 0);
    if (!known) {
        m_currentFolderKey = inboxKey(accountIds.first());
        emit currentFolderKeyChanged();
    }
    const int at = m_folderModel->indexOfKey(m_currentFolderKey);
    const QString name = at >= 0 ? m_folderModel->rows().at(at).name : m_currentFolderName;
    if (name != m_currentFolderName) {
        m_currentFolderName = name;
        emit currentFolderNameChanged();
    }
}

void MailService::setCurrentFolderKey(const QString &key)
{
    if (key == m_currentFolderKey || key.isEmpty()) return;
    const int row = m_folderModel->indexOfKey(key);
    if (row >= 0 && !m_folderModel->rows().at(row).selectable) return;
    m_currentFolderKey = key;
    emit currentFolderKeyChanged();
    if (row >= 0 && m_folderModel->rows().at(row).name != m_currentFolderName) {
        m_currentFolderName = m_folderModel->rows().at(row).name;
        emit currentFolderNameChanged();
    }
    rebuildMessages();
    if (m_messageModel->indexOfMessage(m_selectedMessageId) < 0) setSelectedMessageId(QString());
    const AccountRuntime *rt = runtime(key.section(QLatin1Char(':'), 1, 1));
    if (m_autoSync && rt != nullptr && rt->isImap()) static_cast<void>(syncFolder(key));
}

QString MailService::currentFolderName() const
{
    return m_currentFolderName;
}

void MailService::toggleFolderExpanded(const QString &key)
{
    if (m_collapsed.contains(key)) m_collapsed.remove(key);
    else m_collapsed.insert(key);
    QStringList list(m_collapsed.cbegin(), m_collapsed.cend());
    list.sort();
    saveSetting(QStringLiteral("collapsedFolders"), list);
    rebuildFolders();
}

QVariantList MailService::folderTargets(const QString &accountId) const
{
    const auto counts = folderCounts();
    QStringList ids;
    if (!accountId.isEmpty()) {
        ids.append(accountId);
    } else {
        for (const QindaOffice::Mail::MailAccount &account : m_accountStore->list()) ids.append(account.id);
        ids.append(QString::fromLatin1(kLocalAccountId));
    }
    QVariantList list;
    for (const QString &id : std::as_const(ids)) {
        for (const FolderRow &row : accountFolderRows(id, counts)) {
            if (!row.selectable || row.specialUse == QLatin1String("drafts") || row.specialUse == QLatin1String("outbox"))
                continue;
            list.append(QVariantMap{{QStringLiteral("key"), row.key}, {QStringLiteral("name"), row.name},
                                    {QStringLiteral("depth"), row.depth - 1}, {QStringLiteral("iconName"), row.iconName},
                                    {QStringLiteral("specialUse"), row.specialUse},
                                    {QStringLiteral("accountId"), row.accountId}});
        }
    }
    return list;
}

QVariantMap MailService::draftAsRow(const QVariantMap &draft) const
{
    QVariantMap row = draft;
    const QString id = draft.value(QStringLiteral("id")).toString();
    const QString body = draft.value(QStringLiteral("body")).toString();
    const AccountRuntime *rt = runtime(draft.value(QStringLiteral("accountId")).toString());
    row.insert(QStringLiteral("messageId"), id);
    row.insert(QStringLiteral("threadId"), id);
    row.insert(QStringLiteral("sender"), rt != nullptr ? rt->account().fullName : QString());
    row.insert(QStringLiteral("senderAddress"), rt != nullptr ? rt->account().emailAddress : QString());
    row.insert(QStringLiteral("snippet"), body.simplified().left(160));
    row.insert(QStringLiteral("bodyPlain"), body);
    row.insert(QStringLiteral("date"), draft.value(QStringLiteral("updatedAt")));
    row.insert(QStringLiteral("unread"), false);
    row.insert(QStringLiteral("flagged"), false);
    row.insert(QStringLiteral("isDraft"), true);
    row.insert(QStringLiteral("bodyTruncated"), false);
    return row;
}

QVariantList MailService::draftRows(bool outbox) const
{
    QVariantList rows;
    for (const QVariant &value : m_store->drafts()) {
        const QVariantMap draft = value.toMap();
        if (draft.value(QStringLiteral("queued")).toBool() == outbox) rows.append(draftAsRow(draft));
    }
    return rows;
}

QVariantList MailService::folderRecords(const QString &key) const
{
    const QString local = QString::fromLatin1(kLocalAccountId);
    if (key == folderKey(local, QStringLiteral("Drafts"))) return draftRows(false);
    if (key == folderKey(local, QStringLiteral("Outbox"))) return draftRows(true);
    if (m_organizer->isSearchKey(key)) return m_organizer->searchRecords(key);
    QSet<QString> inboxes;
    if (key == kAllInbox) {
        for (const AccountRuntime *rt : m_runtimes) inboxes.insert(inboxKey(rt->id()));
        inboxes.insert(inboxKey(local));
    }
    QHash<QString, QString> colors;
    for (const QindaOffice::Mail::MailAccount &account : m_accountStore->list())
        colors.insert(account.id, account.extra.value(QStringLiteral("color")).toString());
    QVariantList records;
    for (const QVariant &value : m_store->messages()) {
        QVariantMap record = value.toMap();
        const QString at = folderKeyOf(record);
        if (key == kAllInbox ? !inboxes.contains(at) : at != key) continue;
        const QString owner = ownerAccountOf(record);
        record.insert(QStringLiteral("accountId"), owner);
        record.insert(QStringLiteral("accountColor"), colors.value(owner));
        m_organizer->decorate(&record);   // M81: categories, follow-up, importance
        records.append(record);
    }
    return records;
}

void MailService::rebuildMessages()
{
    m_messageModel->setSource(folderRecords(m_currentFolderKey));
}

QString MailService::searchText() const { return m_messageModel->searchText(); }
void MailService::setSearchText(const QString &text) { m_messageModel->setSearchText(text); }
QString MailService::filter() const { return m_messageModel->filter(); }
void MailService::setFilter(const QString &filter) { m_messageModel->setFilter(filter); }
bool MailService::threaded() const { return m_messageModel->threaded(); }

void MailService::setThreaded(bool threaded)
{
    if (threaded == m_messageModel->threaded()) return;
    m_messageModel->setThreaded(threaded);
    saveSetting(QStringLiteral("threaded"), threaded);
}

QVariantMap MailService::messageById(const QString &messageId) const
{
    if (messageId.isEmpty()) return {};
    for (const QVariant &value : m_store->messages()) {
        QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("id")).toString() != messageId) continue;
        record.insert(QStringLiteral("accountId"), ownerAccountOf(record));
        record.insert(QStringLiteral("folderKey"), folderKeyOf(record));
        record.insert(QStringLiteral("isDraft"), false);
        record.insert(QStringLiteral("bodyTruncated"), record.value(QStringLiteral("bodyTruncated")).toBool());
        if (!record.contains(QStringLiteral("attachments"))) record.insert(QStringLiteral("attachments"), QVariantList());
        m_organizer->decorate(&record);
        return record;
    }
    for (const QVariant &value : m_store->drafts()) {
        const QVariantMap draft = value.toMap();
        if (draft.value(QStringLiteral("id")).toString() == messageId) return draftAsRow(draft);
    }
    return {};
}

QVariantList MailService::conversation(const QString &threadId) const
{
    QList<QVariantMap> list;
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("threadId")).toString() == threadId)
            list.append(messageById(record.value(QStringLiteral("id")).toString()));
    }
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("date")).toString() < b.value(QStringLiteral("date")).toString();
    });
    QVariantList out;
    for (const QVariantMap &record : std::as_const(list)) out.append(record);
    return out;
}

void MailService::setSelectedMessageId(const QString &id)
{
    if (id == m_selectedMessageId) return;
    m_selectedMessageId = id;
    emit selectedMessageIdChanged();
    refreshSelectedMessage();
    m_markReadTimer->stop();
    if (m_markReadDelayMs >= 0 && m_selectedMessage.value(QStringLiteral("unread")).toBool()
        && !m_selectedMessage.value(QStringLiteral("isDraft")).toBool()) {
        m_markReadTimer->start(m_markReadDelayMs);
    }
}

void MailService::refreshSelectedMessage()
{
    QVariantMap current = messageById(m_selectedMessageId);
    if (!m_selectedMessageId.isEmpty() && current.isEmpty()) {
        // Deleted or moved away (a sync, another window): nothing selected.
        m_selectedMessageId.clear();
        m_markReadTimer->stop();
        emit selectedMessageIdChanged();
    }
    if (current == m_selectedMessage) return;
    m_selectedMessage = current;
    emit selectedMessageChanged();
}

void MailService::setMarkReadDelayMs(int ms)
{
    if (ms == m_markReadDelayMs) return;
    m_markReadDelayMs = ms;
    emit markReadDelayMsChanged();
}

} // namespace QindaOffice::Apps::Mail
