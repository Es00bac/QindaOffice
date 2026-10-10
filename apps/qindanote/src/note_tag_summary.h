// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Find Tags (M84, D-093; OneNote's Home ▸ Find Tags): the tagged items of
// this page, this section or the whole notebook, grouped by tag (OneNote's
// default) or by page. Activating an item opens its page with the caret in
// the tagged paragraph; a To Do item can be ticked from the pane.
//
// AGENT-NOTE: the model reads the notebook (pages not yet opened are read
// from disk, like Find); it never writes — ticking goes through the host
// (NoteHost::setTagDone), which keeps the open page's undo history.

#include "note_tags.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Notes {

class NoteHost;

struct TaggedItem {
    QString sectionId;
    QString sectionName;
    QString pageId;
    QString pageTitle;
    Tags::Item item;
};

class NoteTagSummary : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The NoteHost owns the tag summary.")
    // "page" | "section" | "notebook"
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged)
    // "tag" | "page"
    Q_PROPERTY(QString grouping READ grouping WRITE setGrouping NOTIFY groupingChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY resultsChanged)
public:
    enum Roles { KindRole = Qt::UserRole + 1, TextRole, DetailRole, IconRole, ColorRole, TodoRole, DoneRole };
    explicit NoteTagSummary(NoteHost *host);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    [[nodiscard]] QString grouping() const { return m_grouping; }
    void setGrouping(const QString &grouping);
    [[nodiscard]] int itemCount() const { return int(m_items.size()); }
    // Every tagged item in scope, in notebook order.
    [[nodiscard]] const QList<TaggedItem> &items() const { return m_items; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void setDone(int row, bool done);

signals:
    void scopeChanged();
    void groupingChanged();
    void resultsChanged();

private:
    struct Row {
        bool header = false;
        QString text;
        QString detail;
        Tags::TagDef def;
        int item = -1; // index in m_items
    };
    void rebuildRows();

    NoteHost *m_host;
    QString m_scope = QStringLiteral("notebook");
    QString m_grouping = QStringLiteral("tag");
    QList<TaggedItem> m_items;
    QList<Row> m_rows;
};

} // namespace QindaOffice::Apps::Notes
