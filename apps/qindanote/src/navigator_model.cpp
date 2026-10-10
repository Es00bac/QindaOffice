// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigator_model.h"

namespace QindaOffice::Apps::Notes {

NavigatorModel::NavigatorModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void NavigatorModel::setNotebook(Notebook *notebook)
{
    m_notebook = notebook;
    rebuild();
}

bool NavigatorModel::matches(const QString &title) const
{
    return m_filter.isEmpty() || title.contains(m_filter, Qt::CaseInsensitive);
}

void NavigatorModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (m_notebook != nullptr) {
        for (const NoteSection &section : m_notebook->sections()) {
            QList<Row> pages;
            for (const NotePage &page : section.pages) {
                if (matches(page.title)) {
                    pages.append({section.id, page.id, page.title, 0, page.level});
                }
            }
            // A filter hides sections with no matching page; without a
            // filter every section shows, even an empty one.
            if (!m_filter.isEmpty() && pages.isEmpty()) {
                continue;
            }
            m_rows.append({section.id, {}, section.name, int(section.pages.size()), 0});
            if (!m_collapsed.contains(section.id) || !m_filter.isEmpty()) {
                m_rows.append(pages);
            }
        }
    }
    endResetModel();
    emit currentChanged();
}

void NavigatorModel::setCurrent(const QString &sectionId, const QString &pageId)
{
    if (m_currentSectionId == sectionId && m_currentPageId == pageId) {
        return;
    }
    m_currentSectionId = sectionId;
    m_currentPageId = pageId;
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {CurrentRole});
    }
    emit currentChanged();
}

int NavigatorModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NavigatorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case SectionIdRole:
        return row.sectionId;
    case PageIdRole:
        return row.pageId;
    case TitleRole:
    case Qt::DisplayRole:
        return row.title;
    case DepthRole:
        return row.pageId.isEmpty() ? 0 : 1 + row.level;
    case IsSectionRole:
        return row.pageId.isEmpty();
    case ExpandedRole:
        return row.pageId.isEmpty() && !m_collapsed.contains(row.sectionId);
    case CurrentRole:
        return !row.pageId.isEmpty() && row.sectionId == m_currentSectionId
            && row.pageId == m_currentPageId;
    case PageCountRole:
        return row.pageCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> NavigatorModel::roleNames() const
{
    return {{SectionIdRole, "sectionId"}, {PageIdRole, "pageId"},   {TitleRole, "title"},
            {DepthRole, "depth"},         {IsSectionRole, "isSection"}, {ExpandedRole, "expanded"},
            {CurrentRole, "current"},     {PageCountRole, "pageCount"}};
}

void NavigatorModel::setFilter(const QString &filter)
{
    if (m_filter == filter) {
        return;
    }
    m_filter = filter;
    rebuild();
    emit filterChanged();
}

int NavigatorModel::currentRow() const
{
    return rowOf(m_currentSectionId, m_currentPageId);
}

void NavigatorModel::toggleSection(const QString &sectionId)
{
    if (m_collapsed.contains(sectionId)) {
        m_collapsed.remove(sectionId);
    } else {
        m_collapsed.insert(sectionId);
    }
    rebuild();
}

QString NavigatorModel::sectionIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).sectionId : QString();
}

QString NavigatorModel::pageIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).pageId : QString();
}

int NavigatorModel::rowOf(const QString &sectionId, const QString &pageId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).sectionId == sectionId && m_rows.at(i).pageId == pageId) {
            return i;
        }
    }
    return -1;
}

} // namespace QindaOffice::Apps::Notes
