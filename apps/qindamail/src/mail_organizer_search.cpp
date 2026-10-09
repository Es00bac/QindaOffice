// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: search folders — the presets, saving, the folder tree's
// rows and their live contents (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "mail_service.h"

#include "account_store.h"

#include <qindatk/local_productivity_store.h>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

const QString kSearchPrefix = QStringLiteral("search:");
const QString kSearchHeader = QStringLiteral("searches");

QString idOf(const QString &idOrKey)
{
    return idOrKey.startsWith(kSearchPrefix) ? idOrKey.mid(kSearchPrefix.size()) : idOrKey;
}

} // namespace

QVariantList MailOrganizer::searchFolders() const
{
    QVariantList out;
    for (const M::SearchFolder &folder : m_searchFolders) {
        QVariantMap map = M::SearchFolders::toVariantMap(folder);
        map.insert(u"key"_s, kSearchPrefix + folder.id);
        out.append(map);
    }
    return out;
}

QVariantList MailOrganizer::searchFolderPresets() const
{
    const auto row = [](const QString &preset, const QString &group, const QString &argument) {
        return QVariantMap{{u"preset"_s, preset}, {u"name"_s, M::SearchFolders::presetName(preset)},
                           {u"group"_s, group}, {u"argument"_s, argument}};
    };
    const QString reading = tr("Reading Mail"), people = tr("Mail from People and Lists"),
                  organizing = tr("Organizing Mail"), custom = tr("Custom");
    return {row(u"unread"_s, reading, {}), row(u"flagged"_s, reading, {}), row(u"unreadOrFlagged"_s, reading, {}),
            row(u"important"_s, reading, {}), row(u"fromPeople"_s, people, u"people"_s), row(u"toMe"_s, people, {}),
            row(u"categorized"_s, organizing, u"categories"_s), row(u"large"_s, organizing, u"size"_s),
            row(u"old"_s, organizing, u"days"_s), row(u"attachments"_s, organizing, {}),
            row(u"words"_s, organizing, u"words"_s), row(u"custom"_s, custom, {})};
}

QVariantMap MailOrganizer::newSearchFolder(const QString &preset, const QVariant &argument) const
{
    QVariantMap map = M::SearchFolders::toVariantMap(M::SearchFolders::forPreset(preset, argument));
    map.insert(u"key"_s, QString());   // not saved yet
    return map;
}

QVariantMap MailOrganizer::searchFolderByKey(const QString &key) const
{
    for (const M::SearchFolder &folder : m_searchFolders) {
        if (folder.id == idOf(key)) {
            QVariantMap map = M::SearchFolders::toVariantMap(folder);
            map.insert(u"key"_s, kSearchPrefix + folder.id);
            return map;
        }
    }
    return {};
}

QVariantMap MailOrganizer::saveSearchFolder(const QVariantMap &map)
{
    M::SearchFolder folder = M::SearchFolders::fromVariantMap(map);
    if (folder.id.isEmpty()) folder.id = M::SearchFolders::forPreset(u"custom"_s).id;
    const QStringList errors = M::SearchFolders::validate(folder);
    if (!errors.isEmpty()) return {{u"ok"_s, false}, {u"errors"_s, errors}};
    bool replaced = false;
    for (M::SearchFolder &existing : m_searchFolders) {
        if (existing.id == folder.id) {
            existing = folder;
            replaced = true;
        }
    }
    if (!replaced) {
        if (m_searchFolders.size() >= M::SearchFolders::kMaxFolders)
            return {{u"ok"_s, false}, {u"errors"_s, QStringList{tr("There are too many search folders")}}};
        m_searchFolders.append(folder);
    }
    save();
    bump();
    return {{u"ok"_s, true}, {u"id"_s, folder.id}, {u"key"_s, kSearchPrefix + folder.id}, {u"errors"_s, QStringList()}};
}

bool MailOrganizer::deleteSearchFolder(const QString &idOrKey)
{
    const QString id = idOf(idOrKey);
    const qsizetype before = m_searchFolders.size();
    m_searchFolders.removeIf([&id](const M::SearchFolder &folder) { return folder.id == id; });
    if (m_searchFolders.size() == before) return false;
    save();
    bump();
    return true;
}

bool MailOrganizer::isSearchKey(const QString &key) const
{
    if (!key.startsWith(kSearchPrefix)) return false;
    const QString id = idOf(key);
    return std::any_of(m_searchFolders.cbegin(), m_searchFolders.cend(), [&id](const M::SearchFolder &f) { return f.id == id; });
}

QHash<QString, QStringList> MailOrganizer::skippedMailboxes() const
{
    // Trash, Junk, Drafts and the Outbox stay out, as in Outlook.
    QHash<QString, QStringList> skipped;
    for (const QindaOffice::Mail::MailAccount &account : m_service->accountStore()->list()) {
        if (const AccountRuntime *rt = m_service->runtime(account.id))
            skipped.insert(account.id, {rt->specialMailbox(u"trash"_s), rt->specialMailbox(u"junk"_s), rt->specialMailbox(u"drafts"_s)});
    }
    skipped.insert(QString::fromLatin1(kLocalAccountId), {u"Trash"_s, u"Drafts"_s, u"Outbox"_s});
    return skipped;
}

bool MailOrganizer::searchable(const QVariantMap &row, const QHash<QString, QStringList> &skipped) const
{
    const QString owner = m_service->ownerAccountOf(row);
    const QString mailbox = m_service->folderKeyOf(row).mid(5 + owner.size() + 1);   // "acct:" + owner + ":"
    return mailbox.isEmpty() || !skipped.value(owner).contains(mailbox);
}

QVariantList MailOrganizer::searchRecords(const QString &key) const
{
    const QString id = idOf(key);
    const auto folder = std::find_if(m_searchFolders.cbegin(), m_searchFolders.cend(),
                                     [&id](const M::SearchFolder &f) { return f.id == id; });
    if (folder == m_searchFolders.cend()) return {};
    const QHash<QString, QStringList> skipped = skippedMailboxes();
    QHash<QString, QString> colors;
    for (const QindaOffice::Mail::MailAccount &account : m_service->accountStore()->list())
        colors.insert(account.id, account.extra.value(u"color"_s).toString());
    const M::CriteriaContext now = context();
    QVariantList out;
    for (const QVariant &value : m_service->store()->messages()) {
        QVariantMap row = value.toMap();
        if (!searchable(row, skipped) || !M::SearchFolders::matches(*folder, factsOf(row), now)) continue;
        const QString owner = m_service->ownerAccountOf(row);
        row.insert(u"accountId"_s, owner);
        row.insert(u"accountColor"_s, colors.value(owner));
        decorate(&row);
        out.append(row);
    }
    return out;
}

QVector<FolderRow> MailOrganizer::searchRows(bool expanded) const
{
    QVector<FolderRow> rows;
    if (m_searchFolders.isEmpty()) return rows;
    FolderRow header;
    header.key = kSearchHeader;
    header.name = tr("Search Folders");
    header.isAccount = true;
    header.selectable = false;
    header.iconName = u"search"_s;
    header.expanded = expanded;
    if (!expanded) {
        rows.append(header);
        return rows;
    }
    for (const M::SearchFolder &folder : m_searchFolders) {
        FolderRow row;
        row.key = kSearchPrefix + folder.id;
        row.name = folder.name;
        row.path = folder.name;
        row.depth = 1;
        row.specialUse = u"search"_s;
        row.iconName = u"search"_s;
        rows.append(row);
    }
    // One pass over the store: each message's facts once, every folder tested.
    const QHash<QString, QStringList> skipped = skippedMailboxes();
    const M::CriteriaContext now = context();
    for (const QVariant &value : m_service->store()->messages()) {
        const QVariantMap record = value.toMap();
        if (!searchable(record, skipped)) continue;
        const M::MessageFacts facts = factsOf(record);
        for (qsizetype i = 0; i < m_searchFolders.size(); ++i) {
            if (!M::SearchFolders::matches(m_searchFolders.at(i), facts, now)) continue;
            ++rows[i].total;
            rows[i].unread += facts.unread ? 1 : 0;
        }
    }
    header.unread = 0;
    rows.prepend(header);
    return rows;
}

} // namespace QindaOffice::Apps::Mail
