// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_tag_summary.h"

#include "note_host.h"

#include <algorithm>

namespace QindaOffice::Apps::Notes {

NoteTagSummary::NoteTagSummary(NoteHost *host)
    : QAbstractListModel(host)
    , m_host(host)
{
}

int NoteTagSummary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NoteTagSummary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return row.header ? QStringLiteral("group") : QStringLiteral("item");
    case TextRole:
    case Qt::DisplayRole:
        return row.text;
    case DetailRole:
        return row.detail;
    case IconRole:
        if (row.def.todo) {
            return row.item >= 0 && m_items.at(row.item).item.done ? QStringLiteral("square-check")
                                                                   : QStringLiteral("square");
        }
        return row.def.icon.isEmpty() ? QStringLiteral("highlighter") : row.def.icon;
    case ColorRole:
        return row.def.color;
    case TodoRole:
        return row.def.todo && row.item >= 0;
    case DoneRole:
        return row.item >= 0 && m_items.at(row.item).item.done;
    default:
        return {};
    }
}

QHash<int, QByteArray> NoteTagSummary::roleNames() const
{
    return {{KindRole, "kind"},   {TextRole, "text"},      {DetailRole, "detail"}, {IconRole, "icon"},
            {ColorRole, "tagColor"}, {TodoRole, "todo"}, {DoneRole, "done"}};
}

void NoteTagSummary::setScope(const QString &scope)
{
    if (m_scope == scope) {
        return;
    }
    m_scope = scope;
    emit scopeChanged();
    refresh();
}

void NoteTagSummary::setGrouping(const QString &grouping)
{
    if (m_grouping == grouping) {
        return;
    }
    m_grouping = grouping;
    emit groupingChanged();
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit resultsChanged();
}

void NoteTagSummary::refresh()
{
    m_host->flushPersist();
    Notebook *notebook = m_host->noteDocument()->notebook();
    const qreal width = m_host->pageItem() != nullptr ? m_host->pageItem()->pageWidth() : 760;
    QList<TaggedItem> items;
    for (const NoteSection &constSection : notebook->sections()) {
        if (m_scope != QLatin1String("notebook") && constSection.id != m_host->currentSectionId()) {
            continue;
        }
        NoteSection *section = notebook->sectionById(constSection.id);
        for (NotePage &page : section->pages) {
            if (m_scope == QLatin1String("page") && page.id != m_host->currentPageId()) {
                continue;
            }
            // AGENT-GUARD: unopened pages are empty in memory (notebook.h).
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            for (const Tags::Item &item : Tags::items(page.envelope.blocksJson, width)) {
                items.append({section->id, section->name, page.id, page.title, item});
            }
        }
    }
    beginResetModel();
    m_items = items;
    rebuildRows();
    endResetModel();
    emit resultsChanged();
}

void NoteTagSummary::rebuildRows()
{
    m_rows.clear();
    if (m_grouping == QLatin1String("page")) {
        QString page;
        for (int i = 0; i < m_items.size(); ++i) {
            const TaggedItem &tagged = m_items.at(i);
            if (tagged.pageId != page) {
                page = tagged.pageId;
                m_rows.append({true, tagged.pageTitle, tagged.sectionName, {}, -1});
            }
            m_rows.append({false, tagged.item.text, tagged.item.def.name, tagged.item.def, i});
        }
        return;
    }
    // By tag, in OneNote's order: the built-ins, then custom tags by name.
    QStringList order;
    for (const Tags::TagDef &def : Tags::builtIn()) {
        order.append(def.id);
    }
    QStringList custom;
    for (const TaggedItem &tagged : std::as_const(m_items)) {
        if (!order.contains(tagged.item.def.id) && !custom.contains(tagged.item.def.id)) {
            custom.append(tagged.item.def.id);
        }
    }
    std::sort(custom.begin(), custom.end(), [this](const QString &a, const QString &b) {
        QString nameA;
        QString nameB;
        for (const TaggedItem &tagged : std::as_const(m_items)) {
            if (tagged.item.def.id == a) {
                nameA = tagged.item.def.name;
            }
            if (tagged.item.def.id == b) {
                nameB = tagged.item.def.name;
            }
        }
        return nameA.localeAwareCompare(nameB) < 0;
    });
    order += custom;
    for (const QString &id : std::as_const(order)) {
        QList<int> members;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).item.def.id == id) {
                members.append(i);
            }
        }
        if (members.isEmpty()) {
            continue;
        }
        const Tags::TagDef def = m_items.at(members.first()).item.def;
        m_rows.append({true, def.name, tr("%n item(s)", "", int(members.size())), def, -1});
        for (const int i : std::as_const(members)) {
            const TaggedItem &tagged = m_items.at(i);
            m_rows.append({false, tagged.item.text,
                           QStringLiteral("%1 — %2").arg(tagged.sectionName, tagged.pageTitle), def, i});
        }
    }
}

void NoteTagSummary::activate(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).item < 0) {
        return;
    }
    const TaggedItem &tagged = m_items.at(m_rows.at(row).item);
    m_host->navigateTo(tagged.sectionId, tagged.pageId, tagged.item.block, tagged.item.paragraph);
}

void NoteTagSummary::setDone(int row, bool done)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).item < 0) {
        return;
    }
    const TaggedItem tagged = m_items.at(m_rows.at(row).item);
    if (m_host->setTagDone(tagged.sectionId, tagged.pageId, tagged.item.block, tagged.item.entry, done)) {
        refresh();
    }
}

} // namespace QindaOffice::Apps::Notes
