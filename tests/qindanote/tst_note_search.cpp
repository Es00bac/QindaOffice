// SPDX-License-Identifier: GPL-3.0-or-later
// M84 gates for Search across notebooks (D-093): typed and formatted text,
// live-notes Markdown, checklists, tag names and page titles; every word
// must be on the page; results grouped by section and page; the page,
// section, notebook and all-notebooks scopes (another open notebook and a
// recent one on disk); a result opens its page with the caret in the block.

#include "note_test_support.h"

#include "support/quick_dump.h"

#include "paths.h"
#include "recent_files.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_search.h"
#include "qindanote/src/note_tags.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;

namespace {

QByteArray blocksOf(const QJsonArray &blocks)
{
    return withBlocksArray(QByteArray("{}"), blocks);
}

QJsonObject text(const QString &value)
{
    return {{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), value}};
}

// Two sections: "Work" (Budget page, Meeting page) and "Home" (Garden).
void fill(Notebook *notebook)
{
    NoteSection &work = notebook->addSection(QStringLiteral("Work"));
    NotePage &budget = notebook->addPage(work, QStringLiteral("Budget review"));
    QJsonObject flagged = text(QStringLiteral("Quarterly numbers look fine"));
    flagged.insert(QStringLiteral("tags"), QJsonArray{Tags::entryFor(Tags::byId(QStringLiteral("important")), QString())});
    budget.envelope.blocksJson = blocksOf({flagged, QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                                                               {QStringLiteral("text"), QStringLiteral("Send the budget to Ana")}}});
    NotePage &meeting = notebook->addPage(*notebook->sectionById(work.id), QStringLiteral("Team meeting"));
    meeting.envelope.blocksJson = blocksOf({QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
                                                        {QStringLiteral("text"), QStringLiteral("## Decisions\n- the budget grows")}},
                                            QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                                        {QStringLiteral("data"), QString()}}});
    NoteSection &home = notebook->addSection(QStringLiteral("Home"));
    NotePage &garden = notebook->addPage(home, QStringLiteral("Garden"));
    garden.envelope.blocksJson = blocksOf({text(QStringLiteral("Plant tomatoes; no budget left"))});
}

} // namespace

class NoteSearchTest final : public QObject {
    Q_OBJECT

private slots:
    void findsTextTitlesTagsAndNotes()
    {
        Notebook notebook;
        fill(&notebook);
        QList<SearchHit> hits = NoteSearch::searchNotebook(notebook, QStringLiteral("budget"));
        // Budget review: its title and its checklist; Team meeting: its
        // notes; Garden: its text. Grouped in notebook order.
        QStringList pages;
        for (const SearchHit &hit : hits) {
            pages.append(hit.pageTitle + QLatin1Char('/') + hit.kind);
        }
        QCOMPARE(pages, (QStringList{QStringLiteral("Budget review/title"), QStringLiteral("Budget review/check"),
                                     QStringLiteral("Team meeting/markdown"), QStringLiteral("Garden/text")}));
        // Every word must be on the page; case does not matter.
        hits = NoteSearch::searchNotebook(notebook, QStringLiteral("BUDGET ana"));
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.first().pageTitle, QStringLiteral("Budget review"));
        // Tag names are searchable.
        hits = NoteSearch::searchNotebook(notebook, QStringLiteral("important"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().kind, QStringLiteral("tag"));
        QVERIFY(hits.first().snippet.contains(QStringLiteral("Quarterly numbers")));
        // Restricted to a section or a page.
        const QString home = notebook.sections().at(1).id;
        QCOMPARE(NoteSearch::searchNotebook(notebook, QStringLiteral("budget"), home).size(), 1);
        const QString meeting = notebook.sections().first().pages.at(1).id;
        QCOMPARE(NoteSearch::searchNotebook(notebook, QStringLiteral("budget"), QString(), meeting).size(), 1);
        QVERIFY(NoteSearch::searchNotebook(notebook, QStringLiteral("nothing-like-this")).isEmpty());
    }

    void searchesEveryNotebookAndOpensResults()
    {
        // A notebook on disk, only in the recent list…
        QTemporaryDir disk;
        const QString closedPath = disk.filePath(QStringLiteral("Archive"));
        {
            Notebook closed;
            NoteSection &old = closed.addSection(QStringLiteral("Old"));
            NotePage &page = closed.addPage(old, QStringLiteral("Last year"));
            page.envelope.blocksJson = blocksOf({text(QStringLiteral("the budget of last year"))});
            QString error;
            QVERIFY2(closed.create(closedPath, &error), qPrintable(error));
        }
        QindaOffice::Core::RecentFiles recent;
        static_cast<void>(recent.load(QindaOffice::Core::configDirFor(QindaOffice::Core::appLeaf())
                                      + QStringLiteral("/recent.json")));
        recent.add(closedPath);
        QVERIFY(recent.save());
        // … another notebook open in this process …
        NoteHost other;
        Notebook *otherBook = other.noteDocument()->notebook();
        otherBook->addPage(*otherBook->sectionById(otherBook->sections().first().id), QStringLiteral("Plans"))
            .envelope.blocksJson = blocksOf({text(QStringLiteral("budget for the trip"))});
        // … and this window's own.
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        Notebook *own = scene.host.noteDocument()->notebook();
        fill(own);
        NoteSearchModel *search = scene.host.search();
        search->setScope(QStringLiteral("notebook"));
        search->setQuery(QStringLiteral("budget"));
        QCOMPARE(search->hitCount(), 4);
        QCOMPARE(search->pageCount(), 3);
        // Grouped: section row, page row, its hits.
        QCOMPARE(search->data(search->index(0), NoteSearchModel::KindRole).toString(), QStringLiteral("section"));
        QCOMPARE(search->data(search->index(0), NoteSearchModel::TextRole).toString(), QStringLiteral("Work"));
        QCOMPARE(search->data(search->index(1), NoteSearchModel::KindRole).toString(), QStringLiteral("page"));
        QCOMPARE(search->data(search->index(1), NoteSearchModel::TextRole).toString(), QStringLiteral("Budget review"));
        QCOMPARE(search->data(search->index(2), NoteSearchModel::KindRole).toString(), QStringLiteral("hit"));
        search->setScope(QStringLiteral("all"));
        QCOMPARE(search->hitCount(), 6); // + the open "Plans" and the closed "Last year"
        bool sawClosed = false;
        bool sawOther = false;
        for (int row = 0; row < search->rowCount(); ++row) {
            const QString textRow = search->data(search->index(row), NoteSearchModel::TextRole).toString();
            sawClosed = sawClosed || textRow == QStringLiteral("Last year");
            sawOther = sawOther || textRow == QStringLiteral("Plans");
        }
        QVERIFY(sawClosed);
        QVERIFY(sawOther);
        // Ctrl+E shows the pane with the results; a hit opens its page.
        QTest::keyClick(scene.window, Qt::Key_E, Qt::ControlModifier);
        QQuickItem *panel = nullptr;
        QTRY_VERIFY((panel = findItem(scene.window->contentItem(), QStringLiteral("searchPanel"))) != nullptr
                    && panel->isVisible());
        // The keyboard is in the search field (or its inner input).
        QQuickItem *field = findItem(panel, QStringLiteral("noteSearchField"));
        QVERIFY(field != nullptr);
        const auto inField = [&] {
            for (QQuickItem *item = scene.window->activeFocusItem(); item != nullptr; item = item->parentItem()) {
                if (item == field) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(inField());
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindanote_search"));
        int gardenHit = -1;
        for (int row = 0; row < search->rowCount(); ++row) {
            const SearchHit *hit = search->hitAt(row);
            if (hit != nullptr && hit->pageTitle == QStringLiteral("Garden") && hit->block >= 0) {
                gardenHit = row;
            }
        }
        QVERIFY(gardenHit >= 0);
        search->activate(gardenHit);
        QTRY_COMPARE(scene.host.currentPageTitle(), QStringLiteral("Garden"));
        QTRY_COMPARE(scene.page->editingBlock(), 0);
        QCOMPARE(scene.page->editingText(), QStringLiteral("Plant tomatoes; no budget left"));
    }
};

NOTE_TEST_MAIN(NoteSearchTest)

#include "tst_note_search.moc"
