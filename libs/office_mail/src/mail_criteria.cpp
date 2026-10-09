// SPDX-License-Identifier: GPL-3.0-or-later
// Rule and search-folder conditions (mail_criteria.h).
#include "mail_criteria.h"

#include <qindatk/mail_mime.h>

#include <QCoreApplication>
#include <QRegularExpression>

namespace QindaOffice::Mail::Criteria {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::Criteria", text);
}

// Conditions that take a list of words or addresses.
const QStringList &wordTypes()
{
    static const QStringList types{u"from"_s, u"sentTo"_s, u"subject"_s, u"body"_s, u"subjectOrBody"_s, u"header"_s,
                                   u"account"_s};
    return types;
}

const QStringList &knownKeys()
{
    static const QStringList keys{u"type"_s, u"values"_s, u"value"_s, u"min"_s, u"max"_s, u"after"_s, u"before"_s,
                                  u"days"_s};
    return keys;
}

bool containsAny(const QString &haystack, const QStringList &needles)
{
    for (const QString &needle : needles) {
        const QString word = needle.trimmed();
        if (!word.isEmpty() && haystack.contains(word, Qt::CaseInsensitive)) return true;
    }
    return false;
}

QStringList addressesOf(const QString &list)
{
    QString error;
    QStringList out;
    for (const QindaTK::Mime::Address &address : QindaTK::Mime::parseAddressList(list, &error))
        out.append(address.address.toLower());
    if (!error.isEmpty()) {
        // A header the strict parser refuses still names addresses: take
        // every token that looks like one.
        static const QRegularExpression loose(u"[^\\s<>,;\"()]+@[^\\s<>,;\"()]+"_s);
        out.clear();
        for (auto it = loose.globalMatch(list); it.hasNext();) out.append(it.next().captured(0).toLower());
    }
    return out;
}

bool anyMine(const QStringList &addresses, const CriteriaContext &context)
{
    for (const QString &mine : context.myAddresses) {
        if (addresses.contains(mine.trimmed().toLower())) return true;
    }
    return false;
}

QDate localDate(const QDateTime &utc)
{
    return utc.isValid() ? utc.toLocalTime().date() : QDate();
}

QString joined(const QStringList &values)
{
    return values.join(tr(" or "));
}

} // namespace

const QStringList &types()
{
    static const QStringList all{u"from"_s,          u"sentTo"_s,        u"sentOnlyToMe"_s, u"toMe"_s,
                                 u"toOrCcMe"_s,      u"subject"_s,       u"body"_s,         u"subjectOrBody"_s,
                                 u"header"_s,        u"hasAttachment"_s, u"importance"_s,   u"size"_s,
                                 u"received"_s,      u"account"_s,       u"category"_s,     u"flagged"_s,
                                 u"unread"_s,        u"olderThan"_s};
    return all;
}

bool needsHeaders(const QList<Condition> &conditions)
{
    for (const Condition &condition : conditions) {
        if (condition.type == "header"_L1 || condition.type == "importance"_L1) return true;
    }
    return false;
}

Condition fromVariantMap(const QVariantMap &map)
{
    Condition c;
    c.type = map.value(u"type"_s).toString().trimmed();
    const QVariant values = map.value(u"values"_s);
    c.values = values.typeId() == QMetaType::QString ? QStringList{values.toString()} : values.toStringList();
    c.values.removeAll(QString());
    c.value = map.value(u"value"_s).toString();
    c.minimum = map.value(u"min"_s).toLongLong();
    c.maximum = map.value(u"max"_s).toLongLong();
    c.after = QDate::fromString(map.value(u"after"_s).toString(), Qt::ISODate);
    c.before = QDate::fromString(map.value(u"before"_s).toString(), Qt::ISODate);
    c.days = map.value(u"days"_s).toInt();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!knownKeys().contains(it.key())) c.extra.insert(it.key(), it.value());
    }
    return c;
}

QVariantMap toVariantMap(const Condition &condition)
{
    QVariantMap map = condition.extra;
    map.insert(u"type"_s, condition.type);
    if (!condition.values.isEmpty() || wordTypes().contains(condition.type) || condition.type == "category"_L1)
        map.insert(u"values"_s, condition.values);
    if (!condition.value.isEmpty()) map.insert(u"value"_s, condition.value);
    if (condition.type == "size"_L1) {
        map.insert(u"min"_s, condition.minimum);
        map.insert(u"max"_s, condition.maximum);
    }
    if (condition.after.isValid()) map.insert(u"after"_s, condition.after.toString(Qt::ISODate));
    if (condition.before.isValid()) map.insert(u"before"_s, condition.before.toString(Qt::ISODate));
    if (condition.type == "olderThan"_L1) map.insert(u"days"_s, condition.days);
    return map;
}

QList<Condition> listFromVariant(const QVariant &list)
{
    QList<Condition> out;
    for (const QVariant &value : list.toList()) out.append(fromVariantMap(value.toMap()));
    return out;
}

QVariantList listToVariant(const QList<Condition> &conditions)
{
    QVariantList out;
    for (const Condition &condition : conditions) out.append(toVariantMap(condition));
    return out;
}

QStringList validate(const Condition &c, const QString &path)
{
    QStringList problems;
    const auto problem = [&](const QString &field, const QString &text) {
        problems.append(path + (field.isEmpty() ? QString() : u"."_s + field) + u": "_s + text);
    };
    if (!types().contains(c.type)) {
        problem(u"type"_s, tr("unknown condition “%1”").arg(c.type));
        return problems;
    }
    bool emptyWord = false;
    for (const QString &value : c.values) emptyWord = emptyWord || value.trimmed().isEmpty();
    if (wordTypes().contains(c.type) && (c.values.isEmpty() || emptyWord))
        problem(u"values"_s, c.type == "account"_L1 ? tr("choose an account") : tr("add at least one word or address"));
    if (c.values.size() > 256) problem(u"values"_s, tr("at most 256 entries"));
    if (c.type == "importance"_L1 && !QStringList{u"low"_s, u"normal"_s, u"high"_s}.contains(c.value))
        problem(u"value"_s, tr("choose low, normal or high"));
    if (c.type == "size"_L1) {
        if (c.minimum < 0 || c.maximum < 0) problem(u"min"_s, tr("sizes cannot be negative"));
        else if (c.minimum == 0 && c.maximum == 0) problem(u"min"_s, tr("give a smallest or a largest size"));
        else if (c.maximum > 0 && c.minimum > c.maximum) problem(u"max"_s, tr("the largest size is below the smallest"));
    }
    if (c.type == "received"_L1) {
        if (!c.after.isValid() && !c.before.isValid()) problem(u"after"_s, tr("give a first or a last date"));
        else if (c.after.isValid() && c.before.isValid() && c.after > c.before)
            problem(u"before"_s, tr("the last date is before the first"));
    }
    if (c.type == "olderThan"_L1 && (c.days < 1 || c.days > 36500)) problem(u"days"_s, tr("give 1 to 36500 days"));
    return problems;
}

bool matches(const Condition &c, const MessageFacts &f, const CriteriaContext &context)
{
    const QString &t = c.type;
    if (t == "from"_L1) return containsAny(f.senderName + u'\n' + f.senderAddress, c.values);
    if (t == "sentTo"_L1) return containsAny(f.to + u'\n' + f.cc, c.values);
    if (t == "sentOnlyToMe"_L1) {
        const QStringList to = addressesOf(f.to);
        return to.size() == 1 && anyMine(to, context) && addressesOf(f.cc).isEmpty();
    }
    if (t == "toMe"_L1) return anyMine(addressesOf(f.to), context);
    if (t == "toOrCcMe"_L1) return anyMine(addressesOf(f.to) + addressesOf(f.cc), context);
    if (t == "subject"_L1) return containsAny(f.subject, c.values);
    if (t == "body"_L1) return containsAny(f.body, c.values);
    if (t == "subjectOrBody"_L1) return containsAny(f.subject, c.values) || containsAny(f.body, c.values);
    if (t == "header"_L1) return f.headersKnown && containsAny(f.headers, c.values);
    if (t == "hasAttachment"_L1) return f.hasAttachments;
    if (t == "importance"_L1) return f.importance == c.value;
    if (t == "size"_L1) {
        if (f.sizeBytes < 0) return false;
        const qint64 kb = (f.sizeBytes + 1023) / 1024;
        return kb >= c.minimum && (c.maximum == 0 || kb <= c.maximum);
    }
    if (t == "received"_L1) {
        const QDate day = localDate(f.received);
        if (!day.isValid()) return false;
        return (!c.after.isValid() || day >= c.after) && (!c.before.isValid() || day <= c.before);
    }
    if (t == "account"_L1) return c.values.contains(f.accountId);
    if (t == "category"_L1) {
        if (c.values.isEmpty()) return !f.categories.isEmpty();
        for (const QString &name : c.values) {
            if (f.categories.contains(name.trimmed(), Qt::CaseInsensitive)) return true;
        }
        return false;
    }
    if (t == "flagged"_L1) return f.flagged;
    if (t == "unread"_L1) return f.unread;
    if (t == "olderThan"_L1) return f.received.isValid() && c.days > 0 && f.received < context.now.addDays(-c.days);
    return false; // AGENT-GUARD: an unknown type never matches (a newer file read by an older build)
}

bool matchesAll(const QList<Condition> &conditions, const MessageFacts &facts, const CriteriaContext &context)
{
    for (const Condition &condition : conditions) {
        if (!matches(condition, facts, context)) return false;
    }
    return true;
}

bool matchesAny(const QList<Condition> &conditions, const MessageFacts &facts, const CriteriaContext &context)
{
    for (const Condition &condition : conditions) {
        if (matches(condition, facts, context)) return true;
    }
    return false;
}

QString importanceFromHeaders(const QString &headers)
{
    // Unfolded "name: value" fields; the first of Importance, X-Priority,
    // Priority that says something wins.
    QString unfolded = headers;
    unfolded.replace(QRegularExpression(u"\r?\n[ \t]+"_s), u" "_s);
    const auto field = [&unfolded](const QString &name) {
        const QRegularExpression re(u"^"_s + QRegularExpression::escape(name) + u"[ \t]*:[ \t]*(.*)$"_s,
                                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
        const QRegularExpressionMatch m = re.match(unfolded);
        return m.hasMatch() ? m.captured(1).trimmed().toLower() : QString();
    };
    const QString importance = field(u"Importance"_s);
    if (importance.startsWith("high"_L1)) return u"high"_s;
    if (importance.startsWith("low"_L1)) return u"low"_s;
    const QString priority = field(u"X-Priority"_s);
    if (!priority.isEmpty() && priority.at(0).isDigit()) {
        const int level = priority.at(0).digitValue();
        if (level <= 2) return u"high"_s;
        if (level >= 4) return u"low"_s;
    }
    const QString plain = field(u"Priority"_s);
    if (plain.startsWith("urgent"_L1)) return u"high"_s;
    if (plain.startsWith("non-urgent"_L1)) return u"low"_s;
    return u"normal"_s;
}

QString describe(const Condition &c)
{
    const QString &t = c.type;
    if (t == "from"_L1) return tr("from %1").arg(joined(c.values));
    if (t == "sentTo"_L1) return tr("sent to %1").arg(joined(c.values));
    if (t == "sentOnlyToMe"_L1) return tr("sent only to me");
    if (t == "toMe"_L1) return tr("where my name is in the To box");
    if (t == "toOrCcMe"_L1) return tr("where my name is in the To or Cc box");
    if (t == "subject"_L1) return tr("with %1 in the subject").arg(joined(c.values));
    if (t == "body"_L1) return tr("with %1 in the body").arg(joined(c.values));
    if (t == "subjectOrBody"_L1) return tr("with %1 in the subject or body").arg(joined(c.values));
    if (t == "header"_L1) return tr("with %1 in the message header").arg(joined(c.values));
    if (t == "hasAttachment"_L1) return tr("which has an attachment");
    if (t == "importance"_L1) {
        return c.value == "high"_L1 ? tr("marked as high importance")
             : c.value == "low"_L1  ? tr("marked as low importance")
                                    : tr("marked as normal importance");
    }
    if (t == "size"_L1) {
        if (c.maximum == 0) return tr("larger than %1 KB").arg(c.minimum);
        if (c.minimum == 0) return tr("smaller than %1 KB").arg(c.maximum);
        return tr("between %1 and %2 KB").arg(c.minimum).arg(c.maximum);
    }
    if (t == "received"_L1) {
        const QString a = c.after.isValid() ? c.after.toString(Qt::ISODate) : QString();
        const QString b = c.before.isValid() ? c.before.toString(Qt::ISODate) : QString();
        if (b.isEmpty()) return tr("received on or after %1").arg(a);
        if (a.isEmpty()) return tr("received on or before %1").arg(b);
        return tr("received between %1 and %2").arg(a, b);
    }
    if (t == "account"_L1) return tr("through account %1").arg(joined(c.values));
    if (t == "category"_L1)
        return c.values.isEmpty() ? tr("assigned to any category") : tr("assigned to %1").arg(joined(c.values));
    if (t == "flagged"_L1) return tr("flagged for follow up");
    if (t == "unread"_L1) return tr("unread");
    if (t == "olderThan"_L1)
        return QCoreApplication::translate("QindaOffice::Mail::Criteria", "older than %n day(s)", nullptr, c.days);
    return t;
}

} // namespace QindaOffice::Mail::Criteria
