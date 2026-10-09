// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The conditions Outlook's rules, their exceptions and its search folders
// test a message against (M81, D-090; docs/mail.md#rules-and-alerts).
// Pure value code: no store, no network, no QObject — the host builds a
// MessageFacts from a store record (and, when a condition needs them, the
// message's header block) and asks matches().
//
// AGENT-CONTRACT (rules.json / organizer.json, the rule editor's QML,
// search folders): a condition is the map {type, values, value, min, max,
// after, before, days}; unknown keys are kept. Types and their fields:
//   from            values: words/addresses in the sender's name or address
//   sentTo          values: words/addresses in To or Cc
//   sentOnlyToMe    —      (To is exactly one of my addresses, Cc empty)
//   toMe            —      (one of my addresses is in To)
//   toOrCcMe        —      (one of my addresses is in To or Cc)
//   subject         values: words in the subject
//   body            values: words in the body
//   subjectOrBody   values: words in the subject or the body
//   header          values: words in the header block
//   hasAttachment   —
//   importance      value: low | normal | high
//   size            min, max: KB (0 = no bound on that side)
//   received        after, before: yyyy-MM-dd, inclusive local dates
//   account         values: account ids
//   category        values: category names (empty = any category)
//   flagged         —      (flagged for follow-up, not completed)
//   unread          —
//   olderThan       days
// Several values are alternatives (any one matches), as in Outlook; the
// conditions of a rule must all match. Words compare case-insensitively as
// substrings.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace QindaOffice::Mail {

// What the conditions see of one message.
struct MessageFacts {
    QString id;
    QString accountId;
    QString folderKey;           // acct:<account>:<mailbox>
    QString senderName;
    QString senderAddress;
    QString to;                  // the To header as text ("Ada <ada@x>, b@y")
    QString cc;
    QString subject;
    QString body;                // plain text
    QString headers;             // the raw header block; empty when unknown
    bool headersKnown = false;
    bool hasAttachments = false;
    QString importance = QStringLiteral("normal");   // low | normal | high
    qint64 sizeBytes = -1;       // -1: unknown
    QDateTime received;          // UTC
    QStringList categories;
    bool flagged = false;        // follow-up flag set and not completed
    bool unread = false;
};

struct CriteriaContext {
    QStringList myAddresses;     // the user's own addresses (any case)
    QDateTime now = QDateTime::currentDateTimeUtc();
};

struct Condition {
    QString type;
    QStringList values;
    QString value;
    qint64 minimum = 0;          // size, KB
    qint64 maximum = 0;
    QDate after, before;         // received
    int days = 0;                // olderThan
    QVariantMap extra;           // unknown keys, written back
};

namespace Criteria {

// Every condition type, in the order the editor lists them.
const QStringList &types();
// Types whose evaluation reads the header block (header, importance when
// the host derives it from headers): the host fetches headers first.
bool needsHeaders(const QList<Condition> &conditions);

Condition fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const Condition &condition);
QList<Condition> listFromVariant(const QVariant &list);
QVariantList listToVariant(const QList<Condition> &conditions);

// One problem per line, prefixed with `path` ("conditions[2].values: …").
QStringList validate(const Condition &condition, const QString &path);

bool matches(const Condition &condition, const MessageFacts &facts, const CriteriaContext &context);
// All of `conditions` (true for none) / any of them (false for none).
bool matchesAll(const QList<Condition> &conditions, const MessageFacts &facts, const CriteriaContext &context);
bool matchesAny(const QList<Condition> &conditions, const MessageFacts &facts, const CriteriaContext &context);

// "low" / "normal" / "high" from a header block's Importance, X-Priority
// or Priority field (RFC 4021, the de-facto X-Priority 1–5); "normal" when
// none says otherwise.
QString importanceFromHeaders(const QString &headers);

// A short human sentence for the rule list and the editor's summary
// ("from grace@example.org or Ada").
QString describe(const Condition &condition);

} // namespace Criteria
} // namespace QindaOffice::Mail
