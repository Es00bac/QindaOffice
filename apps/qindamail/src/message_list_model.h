// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The message list of the current folder: filtered by a search text and a
// quick filter, newest first, optionally grouped into conversations, with
// the display fields the list delegate needs (dateText, dayGroup for the
// section headers). MailService feeds it the folder's records (setSource);
// the model never reads the store itself.
//
// AGENT-CONTRACT (MessageList.qml): roles messageId, threadId, threadCount,
// threadMessageIds, sender, senderAddress, recipients, subject, snippet,
// date (UTC ISO 8601), dateText, dayGroup, unread, flagged, hasAttachments,
// accountId, accountColor, isDraft. With `threaded` a row is a conversation:
// the newest matching message's fields, `threadCount` messages, unread /
// flagged / hasAttachments when ANY of them is, `threadMessageIds` oldest
// first. Without it every row is one message (threadCount 1).
// M81 (D-090): roles categoryChips [{name, color}], followUpStatus
// ("" | flagged | complete), followUpDue (a short label), importance
// (low | normal | high), all of the newest message; `categoryFilter`
// keeps only rows with that category ("*" = any), and a search term
// "category:<name>" (quotes allowed) matches a message's categories.

#include <QAbstractListModel>
#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Mail {

class MessageListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MailService owns the message list.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // all | unread | flagged | attachments
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool threaded READ threaded WRITE setThreaded NOTIFY threadedChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY categoryFilterChanged)
public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        ThreadIdRole,
        ThreadCountRole,
        ThreadMessageIdsRole,
        SenderRole,
        SenderAddressRole,
        RecipientsRole,
        SubjectRole,
        SnippetRole,
        DateRole,
        DateTextRole,
        DayGroupRole,
        UnreadRole,
        FlaggedRole,
        HasAttachmentsRole,
        AccountIdRole,
        AccountColorRole,
        IsDraftRole,
        CategoryChipsRole,
        FollowUpStatusRole,
        FollowUpDueRole,
        ImportanceRole,
    };

    explicit MessageListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    [[nodiscard]] bool threaded() const { return m_threaded; }
    void setThreaded(bool threaded);
    [[nodiscard]] QString categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QString &category);

    // The folder's records (store `messages` rows, or drafts mapped to the
    // same fields with isDraft) plus accountId/accountColor.
    void setSource(const QVariantList &records);
    // Tests pin "now" (default: the real clock at each rebuild).
    void setReferenceTime(const QDateTime &now);

    [[nodiscard]] Q_INVOKABLE QVariantMap get(int row) const;
    [[nodiscard]] Q_INVOKABLE int indexOfMessage(const QString &messageId) const;

    // Display helpers (static so tests can pin them).
    // today → locale short time; same week → weekday name; else short date.
    [[nodiscard]] static QString dateText(const QDateTime &date, const QDateTime &now, const QLocale &locale);
    // "Today", "Yesterday", "This week" (since the locale's first day of the
    // week), "Earlier" — translated.
    [[nodiscard]] static QString dayGroup(const QDateTime &date, const QDateTime &now, const QLocale &locale);
    // Every whitespace-separated term found (case-insensitive) in sender,
    // senderAddress, subject, snippet or bodyPlain; and the quick filter.
    [[nodiscard]] static bool matches(const QVariantMap &record, const QStringList &terms, const QString &filter);

signals:
    void countChanged();
    void searchTextChanged();
    void filterChanged();
    void threadedChanged();
    void categoryFilterChanged();

private:
    void rebuild();
    [[nodiscard]] QVariantMap rowFor(const QVariantList &messages, const QDateTime &now) const;

    QVariantList m_source;
    QVector<QVariantMap> m_rows;
    QString m_searchText;
    QString m_filter = QStringLiteral("all");
    QString m_categoryFilter;
    bool m_threaded = true;
    QDateTime m_referenceTime;
};

} // namespace QindaOffice::Apps::Mail
