// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Search across notebooks (M84, D-093; OneNote's Ctrl+E): typed text —
// plain and formatted — Markdown (live notes), checklist items, tag names
// and page titles, in this page, this section, this notebook or every
// notebook (the ones open in this process, then the recent ones on disk,
// read without being opened). Results are grouped by notebook section and
// page; activating one opens its page (and its notebook, when it was not
// open) and puts the caret in the block.
//
// Matching: every word of the query, case-insensitive, must occur somewhere
// on the page (its title, blocks or tag names); each block (and the title)
// that holds a word is one hit. Handwriting and pictures carry no text —
// the suite has no OCR — and are not searched.
//
// AGENT-NOTE: the open-notebook registry and the pending navigation are
// process-wide (GUI thread): a QindaNote process owns every notebook window
// it shows (ADR-0110), and a result in a notebook that is not open yet is
// reached when its window has loaded it (NoteHost::documentReplaced).

#include "notebook.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace QindaOffice::Apps::Notes {

class NoteHost;

struct SearchHit {
    QString notebookDir;   // "" for an untitled notebook
    QString notebookName;
    QString sectionId;
    QString sectionName;
    QString pageId;
    QString pageTitle;
    int block = -1;        // -1: the page title
    QString kind;          // title | text | markdown | check | tag
    QString snippet;
    QString tagId;
    // The window showing the notebook, when it is open in this process
    // (untitled notebooks have no directory to tell them apart).
    NoteHost *host = nullptr;
};

namespace NoteSearch {
// Pages of `notebook` (unopened ones are read) whose title or blocks hold
// every word of `query`; restricted to `sectionId` / `pageId` when given.
[[nodiscard]] QList<SearchHit> searchNotebook(Notebook &notebook, const QString &query,
                                              const QString &sectionId = {}, const QString &pageId = {});
void registerHost(NoteHost *host);
void unregisterHost(NoteHost *host);
[[nodiscard]] QList<NoteHost *> openHosts();
struct Navigation {
    QString sectionId;
    QString pageId;
    int block = -1;
};
void requestNavigation(const QString &notebookDir, const Navigation &target);
[[nodiscard]] std::optional<Navigation> takeNavigation(const QString &notebookDir);
} // namespace NoteSearch

// The results pane's model (NoteSearchPanel.qml): section rows, page rows
// and hit rows, in that grouping.
class NoteSearchModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The NoteHost owns the search.")
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // "page" | "section" | "notebook" | "all"
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(int hitCount READ hitCount NOTIFY resultsChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY resultsChanged)
public:
    enum Roles { KindRole = Qt::UserRole + 1, TextRole, DetailRole, MatchRole, IconRole, ColorRole };
    explicit NoteSearchModel(NoteHost *host);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString &query);
    [[nodiscard]] QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    [[nodiscard]] int hitCount() const { return m_hitCount; }
    [[nodiscard]] int pageCount() const { return m_pageCount; }

    Q_INVOKABLE void refresh();
    // Opens the result on row `row` (a page or a hit).
    Q_INVOKABLE void activate(int row);
    [[nodiscard]] const SearchHit *hitAt(int row) const;

signals:
    void queryChanged();
    void scopeChanged();
    void resultsChanged();

private:
    struct Row {
        QString kind; // section | page | hit
        QString text;
        QString detail;
        SearchHit hit;
    };
    NoteHost *m_host;
    QString m_query;
    QString m_scope = QStringLiteral("notebook");
    QList<Row> m_rows;
    int m_hitCount = 0;
    int m_pageCount = 0;
};

} // namespace QindaOffice::Apps::Notes
