// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The notebook's sections and pages as one flat list for the navigator
// (a Tk.Scroll of Tk.TreeRows): section rows at depth 0, page rows at
// depth 1, collapsed sections hide their pages, a filter hides pages whose
// title does not match (and sections with no visible page).
//
// AGENT-GUARD: rows carry section/page IDS resolved through the Notebook
// at use time, never pointers — the notebook's vectors reallocate.

#include "notebook.h"

#include <QAbstractListModel>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Notes {

class NavigatorModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The NoteHost owns the navigator model.")
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString currentSectionId READ currentSectionId NOTIFY currentChanged)
    Q_PROPERTY(QString currentPageId READ currentPageId NOTIFY currentChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentChanged)
public:
    enum Roles {
        SectionIdRole = Qt::UserRole + 1,
        PageIdRole,
        TitleRole,
        DepthRole,
        IsSectionRole,
        ExpandedRole,
        CurrentRole,
        PageCountRole,
    };
    explicit NavigatorModel(QObject *parent = nullptr);

    void setNotebook(Notebook *notebook);
    // Re-reads the notebook (structure or titles changed).
    void rebuild();
    void setCurrent(const QString &sectionId, const QString &pageId);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    [[nodiscard]] QString currentSectionId() const { return m_currentSectionId; }
    [[nodiscard]] QString currentPageId() const { return m_currentPageId; }
    [[nodiscard]] int currentRow() const;

    Q_INVOKABLE void toggleSection(const QString &sectionId);
    Q_INVOKABLE QString sectionIdAt(int row) const;
    Q_INVOKABLE QString pageIdAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &sectionId, const QString &pageId) const;

signals:
    void filterChanged();
    void currentChanged();

private:
    struct Row {
        QString sectionId;
        QString pageId; // empty for a section row
        QString title;
        int pageCount = 0;
    };
    [[nodiscard]] bool matches(const QString &title) const;

    Notebook *m_notebook = nullptr;
    QList<Row> m_rows;
    QSet<QString> m_collapsed;
    QString m_filter;
    QString m_currentSectionId;
    QString m_currentPageId;
};

} // namespace QindaOffice::Apps::Notes
