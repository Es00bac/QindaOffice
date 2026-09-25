// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_list_model.h"

#include <QHash>
#include <algorithm>

namespace QindaOffice::Apps::Mail {
namespace {

QDateTime dateOf(const QVariantMap &record)
{
    return QDateTime::fromString(record.value(QStringLiteral("date")).toString(), Qt::ISODate);
}

bool newerFirst(const QVariantMap &a, const QVariantMap &b)
{
    const QDateTime da = dateOf(a);
    const QDateTime db = dateOf(b);
    if (da != db) {
        return da > db;
    }
    return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
}

bool hasAttachments(const QVariantMap &record)
{
    return !record.value(QStringLiteral("attachments")).toList().isEmpty();
}

QDate startOfWeek(const QDate &day, const QLocale &locale)
{
    const int first = int(locale.firstDayOfWeek());
    return day.addDays(-((day.dayOfWeek() - first + 7) % 7));
}

} // namespace

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {MessageIdRole, "messageId"},
        {ThreadIdRole, "threadId"},
        {ThreadCountRole, "threadCount"},
        {ThreadMessageIdsRole, "threadMessageIds"},
        {SenderRole, "sender"},
        {SenderAddressRole, "senderAddress"},
        {RecipientsRole, "recipients"},
        {SubjectRole, "subject"},
        {SnippetRole, "snippet"},
        {DateRole, "date"},
        {DateTextRole, "dateText"},
        {DayGroupRole, "dayGroup"},
        {UnreadRole, "unread"},
        {FlaggedRole, "flagged"},
        {HasAttachmentsRole, "hasAttachments"},
        {AccountIdRole, "accountId"},
        {AccountColorRole, "accountColor"},
        {IsDraftRole, "isDraft"},
    };
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    if (role == Qt::DisplayRole) {
        role = SubjectRole;
    }
    const QByteArray name = roleNames().value(role);
    return name.isEmpty() ? QVariant() : m_rows.at(index.row()).value(QString::fromLatin1(name));
}

void MessageListModel::setSearchText(const QString &text)
{
    if (text == m_searchText) {
        return;
    }
    m_searchText = text;
    emit searchTextChanged();
    rebuild();
}

void MessageListModel::setFilter(const QString &filter)
{
    const QString value = filter.isEmpty() ? QStringLiteral("all") : filter;
    if (value == m_filter) {
        return;
    }
    m_filter = value;
    emit filterChanged();
    rebuild();
}

void MessageListModel::setThreaded(bool threaded)
{
    if (threaded == m_threaded) {
        return;
    }
    m_threaded = threaded;
    emit threadedChanged();
    rebuild();
}

void MessageListModel::setSource(const QVariantList &records)
{
    m_source = records;
    rebuild();
}

void MessageListModel::setReferenceTime(const QDateTime &now)
{
    m_referenceTime = now;
    rebuild();
}

QString MessageListModel::dateText(const QDateTime &date, const QDateTime &now, const QLocale &locale)
{
    if (!date.isValid()) {
        return {};
    }
    const QDateTime local = date.toLocalTime();
    const QDate today = now.toLocalTime().date();
    if (local.date() >= today) {
        return locale.toString(local.time(), QLocale::ShortFormat);
    }
    if (local.date() >= startOfWeek(today, locale)) {
        return locale.dayName(local.date().dayOfWeek(), QLocale::ShortFormat);
    }
    return locale.toString(local.date(), QLocale::ShortFormat);
}

QString MessageListModel::dayGroup(const QDateTime &date, const QDateTime &now, const QLocale &locale)
{
    const QDate today = now.toLocalTime().date();
    const QDate day = date.isValid() ? date.toLocalTime().date() : QDate();
    if (day.isValid() && day >= today) {
        return tr("Today");
    }
    if (day.isValid() && day == today.addDays(-1)) {
        return tr("Yesterday");
    }
    if (day.isValid() && day >= startOfWeek(today, locale)) {
        return tr("This week");
    }
    return tr("Earlier");
}

bool MessageListModel::matches(const QVariantMap &record, const QStringList &terms, const QString &filter)
{
    if (filter == QLatin1String("unread") && !record.value(QStringLiteral("unread")).toBool()) {
        return false;
    }
    if (filter == QLatin1String("flagged") && !record.value(QStringLiteral("flagged")).toBool()) {
        return false;
    }
    if (filter == QLatin1String("attachments") && !hasAttachments(record)) {
        return false;
    }
    if (terms.isEmpty()) {
        return true;
    }
    const QString haystack = QStringList{record.value(QStringLiteral("sender")).toString(),
                                         record.value(QStringLiteral("senderAddress")).toString(),
                                         record.value(QStringLiteral("subject")).toString(),
                                         record.value(QStringLiteral("snippet")).toString(),
                                         record.value(QStringLiteral("bodyPlain")).toString()}
                                 .join(QLatin1Char('\n'));
    return std::all_of(terms.cbegin(), terms.cend(), [&haystack](const QString &term) {
        return haystack.contains(term, Qt::CaseInsensitive);
    });
}

QVariantMap MessageListModel::rowFor(const QVariantList &messages, const QDateTime &now) const
{
    // `messages` is newest first; the row shows the newest.
    const QVariantMap newest = messages.first().toMap();
    bool unread = false;
    bool flagged = false;
    bool attachments = false;
    QStringList ids;
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        const QVariantMap message = it->toMap();
        unread = unread || message.value(QStringLiteral("unread")).toBool();
        flagged = flagged || message.value(QStringLiteral("flagged")).toBool();
        attachments = attachments || hasAttachments(message);
        ids.append(message.value(QStringLiteral("id")).toString());
    }
    const QDateTime date = dateOf(newest);
    const QLocale locale;
    const QString id = newest.value(QStringLiteral("id")).toString();
    const QString thread = newest.value(QStringLiteral("threadId")).toString();
    return {
        {QStringLiteral("messageId"), id},
        {QStringLiteral("threadId"), thread.isEmpty() ? id : thread},
        {QStringLiteral("threadCount"), int(messages.size())},
        {QStringLiteral("threadMessageIds"), ids},
        {QStringLiteral("sender"), newest.value(QStringLiteral("sender"))},
        {QStringLiteral("senderAddress"), newest.value(QStringLiteral("senderAddress"))},
        {QStringLiteral("recipients"), newest.value(QStringLiteral("to"))},
        {QStringLiteral("subject"), newest.value(QStringLiteral("subject"))},
        {QStringLiteral("snippet"), newest.value(QStringLiteral("snippet"))},
        {QStringLiteral("date"), newest.value(QStringLiteral("date"))},
        {QStringLiteral("dateText"), dateText(date, now, locale)},
        {QStringLiteral("dayGroup"), dayGroup(date, now, locale)},
        {QStringLiteral("unread"), unread},
        {QStringLiteral("flagged"), flagged},
        {QStringLiteral("hasAttachments"), attachments},
        {QStringLiteral("accountId"), newest.value(QStringLiteral("accountId"))},
        {QStringLiteral("accountColor"), newest.value(QStringLiteral("accountColor"))},
        {QStringLiteral("isDraft"), newest.value(QStringLiteral("isDraft")).toBool()},
    };
}

void MessageListModel::rebuild()
{
    const QDateTime now = m_referenceTime.isValid() ? m_referenceTime : QDateTime::currentDateTime();
    const QStringList terms = m_searchText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<QVariantMap> matching;
    for (const QVariant &value : std::as_const(m_source)) {
        const QVariantMap record = value.toMap();
        if (matches(record, terms, m_filter)) {
            matching.append(record);
        }
    }
    std::sort(matching.begin(), matching.end(), newerFirst);

    QVector<QVariantMap> rows;
    if (m_threaded) {
        // Conversations in order of their newest matching message.
        QHash<QString, qsizetype> slot;
        QList<QVariantList> groups;
        for (const QVariantMap &record : std::as_const(matching)) {
            QString thread = record.value(QStringLiteral("threadId")).toString();
            if (thread.isEmpty()) {
                thread = record.value(QStringLiteral("id")).toString();
            }
            const auto found = slot.constFind(thread);
            if (found == slot.cend()) {
                slot.insert(thread, groups.size());
                groups.append(QVariantList{record});
            } else {
                groups[*found].append(record);
            }
        }
        for (const QVariantList &group : std::as_const(groups)) {
            rows.append(rowFor(group, now));
        }
    } else {
        for (const QVariantMap &record : std::as_const(matching)) {
            rows.append(rowFor(QVariantList{record}, now));
        }
    }

    bool sameIds = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameIds && i < rows.size(); ++i) {
        sameIds = rows.at(i).value(QStringLiteral("messageId")) == m_rows.at(i).value(QStringLiteral("messageId"));
    }
    if (sameIds) {
        // Same rows in the same order (a flag or read state changed): keep
        // the view's scroll position and current item.
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows.at(i) != m_rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(int(i)), index(int(i)));
            }
        }
        return;
    }
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = rows;
    endResetModel();
    if (countChanges) {
        emit countChanged();
    }
}

QVariantMap MessageListModel::get(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap();
}

int MessageListModel::indexOfMessage(const QString &messageId) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        const QVariantMap &row = m_rows.at(i);
        if (row.value(QStringLiteral("messageId")).toString() == messageId
            || row.value(QStringLiteral("threadMessageIds")).toStringList().contains(messageId)) {
            return int(i);
        }
    }
    return -1;
}

} // namespace QindaOffice::Apps::Mail
