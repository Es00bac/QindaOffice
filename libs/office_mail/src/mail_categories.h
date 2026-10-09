// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's color categories (M81, D-090; docs/mail.md#categories): a named,
// coloured label kept in a master list per mailbox, assigned by name to
// mail, events, tasks and contacts. Pure value code plus the two server
// mappings that need no network: Outlook's 25 preset colours (Graph
// `preset0`…`preset24`, EWS CategoryList `color` 0…24) and IMAP keywords.
//
// AGENT-CONTRACT (organizer.json "categories", the QML dialogs, the
// mail_exchange adapters, the IMAP keyword job):
// - a category is {name, color "#rrggbb", preset 0..24 or -1}; names are
//   compared case-insensitively (as Outlook does) and kept as typed;
// - a record's `categories` is a list of names;
// - IMAP: keywordFor(name) is an RFC 3501 flag-keyword atom — spaces become
//   "_", and "_", "&", atom-specials, a leading "$" or "\" and non-ASCII
//   characters go into "&…-" runs of RFC 3501 §5.1.3's modified UTF-7
//   (here also for the printable ASCII a keyword cannot hold, which mailbox
//   names never encode), so nameForKeyword() reads every keyword this code
//   writes back exactly.
//   Keywords starting with "$" (RFC 5788 registry: $Forwarded, $Junk, …)
//   or "\\" are never categories.

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct Category {
    QString name;
    QString color;               // "#rrggbb"
    int preset = -1;             // Outlook preset 0..24, -1 none
    QVariantMap extra;           // unknown keys (and a server id, "serverId")
};

namespace Categories {

inline constexpr int kMaxCategories = 256;
inline constexpr int kMaxNameLength = 255;   // Graph's limit for displayName

struct Preset {
    const char *name;            // Outlook's English name (Red, Orange, …)
    const char *color;           // "#rrggbb"
};
// Outlook's 25 colours in preset order (preset0 = Red … preset24 = DarkCranberry).
const QList<Preset> &presets();
QString colorForPreset(int preset);
// The preset nearest to `color` (RGB distance); -1 for an invalid colour.
int presetForColor(const QString &color);

// Outlook's six starting categories (Red … Purple category).
QList<Category> defaults();

Category fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const Category &category);
QList<Category> listFromVariant(const QVariant &list);
QVariantList listToVariant(const QList<Category> &categories);

// Index of `name` in `list` (case-insensitive), -1 when absent.
int indexOf(const QList<Category> &list, const QString &name);
// Trimmed, empty and duplicate (case-insensitive) names dropped, order kept.
QStringList normalized(const QStringList &names);
// One problem per line ("name: …").
QStringList validate(const Category &category, const QList<Category> &others = {});

// IMAP flag keywords (see the contract above).
QString keywordFor(const QString &name);
// The category a keyword stands for: a known category whose keyword it is,
// else the decoded keyword; "" for system flags and "$" keywords.
QString nameForKeyword(const QString &keyword, const QList<Category> &known = {});
bool isCategoryKeyword(const QString &keyword);

} // namespace Categories
} // namespace QindaOffice::Mail
