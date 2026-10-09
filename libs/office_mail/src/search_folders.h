// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's Search Folders (M81, D-090; docs/mail.md#search-folders): a
// saved search shown in the folder tree whose contents follow the mail as
// it changes. A folder is a set of conditions (mail_criteria.h) that must
// all — or, for "unread or flagged", any — match, over every account or
// some of them.
//
// AGENT-CONTRACT (organizer.json "searchFolders", the folder tree's
// `search:<id>` keys, SearchFolderDialog.qml): {id, name, preset, match
// "all" | "any", conditions [Condition], accounts [account ids, empty =
// all]}. Presets (Outlook's New Search Folder list) and the condition they
// make from `argument`:
//   unread · flagged · unreadOrFlagged · important · fromPeople (people) ·
//   toMe · attachments · categorized (names, empty = any) · large (KB) ·
//   old (days) · words (words in the subject or body) · custom
// Search folders never list Trash, Junk, Drafts or the Outbox (the host
// skips those folders before matching).

#include "mail_criteria.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct SearchFolder {
    QString id;
    QString name;
    QString preset;
    QString match = QStringLiteral("all");
    QList<Condition> conditions;
    QStringList accounts;
    QVariantMap extra;
};

namespace SearchFolders {

inline constexpr int kMaxFolders = 100;

const QStringList &presets();
// The default name of a preset ("Unread Mail", "Large Mail", …).
QString presetName(const QString &preset);
// A folder for `preset`; `argument` is the preset's parameter (people or
// words as a list, a size in KB, days, category names). "custom" gives an
// empty condition list for the editor to fill.
SearchFolder forPreset(const QString &preset, const QVariant &argument = {});

SearchFolder fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const SearchFolder &folder);
QList<SearchFolder> listFromVariant(const QVariant &list);
QVariantList listToVariant(const QList<SearchFolder> &folders);

QStringList validate(const SearchFolder &folder);

// The account scope, then the conditions.
bool matches(const SearchFolder &folder, const MessageFacts &facts, const CriteriaContext &context);

} // namespace SearchFolders
} // namespace QindaOffice::Mail
