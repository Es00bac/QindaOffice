// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The folder pane of QindaMail as a flattened tree (one row per visible
// node, `depth` for the indent). MailService computes the rows from the
// accounts, their mailbox lists and the store's counts; this model only
// holds them and reports minimal changes, so a count update does not reset
// the pane's scroll position or keyboard focus.
//
// AGENT-CONTRACT (FolderPane.qml, MailService::currentFolderKey): row keys
// are `all:inbox` (the unified inboxes row, first), `acct:<accountId>` (an
// account header, isAccount true, not selectable) and
// `acct:<accountId>:<mailbox>` where <mailbox> is the mailbox name the
// provider and the records use (IMAP: the decoded UTF-8 name, which is
// `remoteMailbox` on every record; POP3 and the local account: `Inbox`/
// `INBOX`, `Archive`, `Drafts`, `Outbox`, `Sent`, `Trash`). Keys are stable
// across restarts and are what the lead's QML stores.

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Mail {

struct FolderRow {
    QString key;
    QString accountId;
    QString name;
    QString path;
    int depth = 0;
    QString specialUse;   // inbox | sent | drafts | outbox | trash | junk | archive | all | flagged | ""
    QString iconName;     // a QindaTK (Lucide) icon name
    int unread = 0;
    int total = 0;
    bool selectable = true;
    bool isAccount = false;
    bool expanded = true;

    bool operator==(const FolderRow &other) const = default;
};

class FolderModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MailService owns the folder model.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        AccountIdRole,
        NameRole,
        PathRole,
        DepthRole,
        SpecialUseRole,
        IconNameRole,
        UnreadRole,
        TotalRole,
        SelectableRole,
        IsAccountRole,
        ExpandedRole,
    };

    explicit FolderModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Replaces the rows: dataChanged when the key sequence is unchanged,
    // otherwise a reset.
    void setRows(const QVector<FolderRow> &rows);
    [[nodiscard]] const QVector<FolderRow> &rows() const { return m_rows; }
    [[nodiscard]] Q_INVOKABLE int indexOfKey(const QString &key) const;
    // The row as a map with the role names as keys ({} when out of range).
    [[nodiscard]] Q_INVOKABLE QVariantMap get(int row) const;

    // The icon for a mailbox role (inbox → "inbox", sent → "send", …).
    [[nodiscard]] static QString iconFor(const QString &specialUse);

signals:
    void countChanged();

private:
    QVector<FolderRow> m_rows;
};

} // namespace QindaOffice::Apps::Mail
