// SPDX-License-Identifier: GPL-3.0-or-later
#include "folder_model.h"

namespace QindaOffice::Apps::Mail {

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const FolderRow &row = m_rows.at(index.row());
    switch (role) {
    case KeyRole: return row.key;
    case AccountIdRole: return row.accountId;
    case Qt::DisplayRole:
    case NameRole: return row.name;
    case PathRole: return row.path;
    case DepthRole: return row.depth;
    case SpecialUseRole: return row.specialUse;
    case IconNameRole: return row.iconName;
    case UnreadRole: return row.unread;
    case TotalRole: return row.total;
    case SelectableRole: return row.selectable;
    case IsAccountRole: return row.isAccount;
    case ExpandedRole: return row.expanded;
    default: return {};
    }
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {AccountIdRole, "accountId"},
        {NameRole, "name"},
        {PathRole, "path"},
        {DepthRole, "depth"},
        {SpecialUseRole, "specialUse"},
        {IconNameRole, "iconName"},
        {UnreadRole, "unread"},
        {TotalRole, "total"},
        {SelectableRole, "selectable"},
        {IsAccountRole, "isAccount"},
        {ExpandedRole, "expanded"},
    };
}

void FolderModel::setRows(const QVector<FolderRow> &rows)
{
    bool sameKeys = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameKeys && i < rows.size(); ++i) {
        sameKeys = rows.at(i).key == m_rows.at(i).key;
    }
    if (!sameKeys) {
        const bool countChanges = rows.size() != m_rows.size();
        beginResetModel();
        m_rows = rows;
        endResetModel();
        if (countChanges) {
            emit countChanged();
        }
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (!(rows.at(i) == m_rows.at(i))) {
            m_rows[i] = rows.at(i);
            const QModelIndex at = index(int(i));
            emit dataChanged(at, at);
        }
    }
}

int FolderModel::indexOfKey(const QString &key) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key == key) {
            return int(i);
        }
    }
    return -1;
}

QVariantMap FolderModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    QVariantMap map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return map;
}

QString FolderModel::iconFor(const QString &specialUse)
{
    if (specialUse == QLatin1String("inbox")) return QStringLiteral("inbox");
    if (specialUse == QLatin1String("sent")) return QStringLiteral("send");
    if (specialUse == QLatin1String("drafts")) return QStringLiteral("square-pen");
    if (specialUse == QLatin1String("outbox")) return QStringLiteral("upload");
    if (specialUse == QLatin1String("trash")) return QStringLiteral("trash-2");
    if (specialUse == QLatin1String("junk")) return QStringLiteral("mail-warning");
    if (specialUse == QLatin1String("archive")) return QStringLiteral("archive");
    if (specialUse == QLatin1String("all")) return QStringLiteral("mails");
    if (specialUse == QLatin1String("flagged")) return QStringLiteral("flag");
    return QStringLiteral("folder");
}

} // namespace QindaOffice::Apps::Mail
