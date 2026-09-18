// SPDX-License-Identifier: GPL-3.0-or-later
// The notebook engine and the host: a notebook survives reopen, lazily
// loaded pages are never blanked by a save, recovery round-trips, page
// structure commands work through the host and find lands on the page
// holding the text — none of it needs the window.

#include "note_test_support.h"

#include <QFile>
#include <QSignalSpy>

using namespace QindaOffice::Apps::Notes;
namespace Ink = QindaOffice::Ink;

class QindaNoteTest final : public QObject {
    Q_OBJECT

    static Ink::Stroke stroke(const QPointF &from, const QPointF &to)
    {
        Ink::Stroke s;
        s.samples = {{from, 0.7, 0, 0}, {to, 0.7, 0, 0}};
        s.complete = true;
        return s;
    }

private slots:
    void freshHostOpensOnAWritablePage()
    {
        NoteHost host;
        // A pristine notebook used to be completely empty, so the navigator
        // had nothing in it and the page surface was bound to no page.
        QCOMPARE(host.noteDocument()->notebook()->sections().size(), 1);
        QVERIFY(host.currentPage() != nullptr);
        QVERIFY2(host.statusInfo().contains(QStringLiteral("page 1 of 1")), qPrintable(host.statusInfo()));
        // AGENT-CONTRACT (ADR-0110): untouched, the host is PRISTINE so
        // File > Open can reuse the window.
        QVERIFY(!host.isDirty());
        QVERIFY(host.isPristineUntitled());
        QVERIFY(host.navigator()->rowCount() >= 2); // the section and its page
    }

    void notebookSurvivesReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/Work");
        QString error;
        {
            NoteDocument doc;
            QVERIFY2(doc.save(path, &error), qPrintable(error));
            Notebook *notebook = doc.notebook();
            NoteSection *section = notebook->sectionById(notebook->sections().first().id);
            NotePage &page = section->pages.first();
            QVERIFY(notebook->loadPage(*section, page, &error));
            page.envelope.ink.appendStroke(stroke(QPointF(10, 10), QPointF(200, 120)));
            page.envelope.blocksJson = R"({"blocks":[{"type":"text","text":"remember milk"}]})";
            QVERIFY(notebook->persistPage(*section, page, &error));
            QVERIFY(notebook->save(&error));
        }
        NoteDocument doc;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        Notebook *notebook = doc.notebook();
        NoteSection *section = notebook->sectionById(notebook->sections().first().id);
        NotePage &page = section->pages.first();
        QVERIFY(notebook->loadPage(*section, page, &error));
        QCOMPARE(page.envelope.ink.strokes().size(), 1);
        QVERIFY(page.envelope.blocksJson.contains("remember milk"));
    }

    // AGENT-GUARD regression: pages load lazily, so an unopened page's
    // envelope is EMPTY in memory. save() used to write every page
    // unconditionally — viewing one page and pressing Ctrl+S replaced every
    // other page's file with a blank envelope.
    void savingNeverBlanksPagesThatWereNeverOpened()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/Notes");
        QString error;
        {
            NoteDocument doc;
            QVERIFY2(doc.save(path, &error), qPrintable(error));
            Notebook *book = doc.notebook();
            NoteSection *section = book->sectionById(book->sections().first().id);
            NotePage &second = book->addPage(*section, QStringLiteral("Second"));
            second.envelope.ink.appendStroke(stroke(QPointF(10, 10), QPointF(40, 40)));
            QVERIFY2(doc.save(path, &error), qPrintable(error));
        }
        {
            NoteDocument doc;
            QVERIFY2(doc.load(path, &error), qPrintable(error));
            Notebook *book = doc.notebook();
            NoteSection *section = book->sectionById(book->sections().first().id);
            QCOMPARE(section->pages.size(), 2);
            QVERIFY(!section->pages.at(1).loaded);
            QVERIFY(book->loadPage(*section, section->pages[0], &error));
            QVERIFY2(doc.save(path, &error), qPrintable(error));
        }
        NoteDocument doc;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        Notebook *book = doc.notebook();
        NoteSection *section = book->sectionById(book->sections().first().id);
        QVERIFY(book->loadPage(*section, section->pages[1], &error));
        QCOMPARE(section->pages.at(1).envelope.ink.strokes().size(), 1);
    }

    void recoverySnapshotRoundTripsTheNotebook()
    {
        NoteDocument doc;
        Notebook *book = doc.notebook();
        NoteSection &section = book->addSection(QStringLiteral("Ideas"));
        NotePage &page = book->addPage(section, QStringLiteral("Sketch"));
        page.envelope.ink.appendStroke(stroke(QPointF(3, 4), QPointF(9, 12)));
        page.envelope.background = QStringLiteral("grid");
        const QByteArray payload = doc.recoverySnapshot();
        QVERIFY(!payload.isEmpty());
        NoteDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        Notebook *back = restored.notebook();
        QCOMPARE(back->sections().size(), 2);
        QCOMPARE(back->sections().last().name, QStringLiteral("Ideas"));
        const NotePage &recovered = back->sections().last().pages.first();
        QCOMPARE(recovered.title, QStringLiteral("Sketch"));
        QCOMPARE(recovered.envelope.background, QStringLiteral("grid"));
        QCOMPARE(recovered.envelope.ink.strokes().size(), 1);
        QVERIFY(restored.isDirty());
    }

    void deletingAPageRemovesItsFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/Work");
        NoteDocument doc;
        Notebook *notebook = doc.notebook();
        NoteSection *section = notebook->sectionById(notebook->sections().first().id);
        const QString sectionId = section->id;
        const QString extra = notebook->addPage(*section, QStringLiteral("Scratch")).id;
        QVERIFY(doc.save(path));
        const QString file = path + QStringLiteral("/sections/%1/pages/%2.json").arg(sectionId, extra);
        QVERIFY2(QFile::exists(file), qPrintable(file));
        QVERIFY(notebook->removePage(sectionId, extra));
        // An index entry removed without its file leaves an orphan the next
        // load would resurrect.
        QVERIFY(!QFile::exists(file));
        QCOMPARE(notebook->sectionById(sectionId)->pages.size(), 1);
    }

    void structureCommandsGoThroughTheHost()
    {
        NoteHost host;
        Notebook *book = host.noteDocument()->notebook();
        const QString firstSection = host.currentSectionId();
        const QString firstPage = host.currentPageId();

        host.actions()->trigger(QStringLiteral("notePageNew"));
        QCOMPARE(book->sectionById(firstSection)->pages.size(), 2);
        QVERIFY(host.currentPageId() != firstPage); // lands on the new page
        QVERIFY(host.isDirty());

        // New section: a name answered by the window's prompt.
        QSignalSpy prompt(&host, &NoteHost::newSectionRequested);
        host.actions()->trigger(QStringLiteral("noteSectionNew"));
        QCOMPARE(prompt.count(), 1);
        host.createSection(QStringLiteral("Research"));
        QCOMPARE(book->sections().size(), 2);
        QCOMPARE(book->sections().last().name, QStringLiteral("Research"));
        QVERIFY2(host.statusInfo().startsWith(QStringLiteral("Research")), qPrintable(host.statusInfo()));
        QCOMPARE(host.currentSectionId(), book->sections().last().id);

        // Next/Previous walk every page in order and wrap across sections.
        host.actions()->trigger(QStringLiteral("notePageNext"));
        QCOMPARE(host.currentSectionId(), firstSection);
        QCOMPARE(host.currentPageId(), firstPage);
        host.actions()->trigger(QStringLiteral("notePagePrevious"));
        QCOMPARE(host.currentSectionId(), book->sections().last().id);

        // Rename through the prompt's answer.
        host.renamePage(host.currentSectionId(), host.currentPageId(), QStringLiteral("Sources"));
        QCOMPARE(host.currentPage()->title, QStringLiteral("Sources"));
        QCOMPARE(host.currentPageTitle(), QStringLiteral("Sources"));

        // Move: the second page of the first section up, then refuse past
        // the top (a silent no-op that reports success made Move Up look
        // broken).
        host.selectPage(firstSection, book->sectionById(firstSection)->pages.last().id);
        const QString moved = host.currentPageId();
        host.actions()->trigger(QStringLiteral("notePageMoveUp"));
        QCOMPARE(book->sectionById(firstSection)->pages.first().id, moved);
        QVERIFY(!host.actions()->entry(QStringLiteral("notePageMoveUp"))->isEnabled());

        // Duplicate lands right after its source with the same ink.
        host.currentPage()->envelope.ink.appendStroke(stroke(QPointF(1, 1), QPointF(5, 5)));
        host.actions()->trigger(QStringLiteral("notePageDuplicate"));
        QCOMPARE(book->sectionById(firstSection)->pages.size(), 3);
        QCOMPARE(book->sectionById(firstSection)->pages.at(1).id, host.currentPageId());
        QCOMPARE(host.currentPage()->envelope.ink.strokes().size(), 1);
        QVERIFY(host.currentPage()->title.endsWith(QStringLiteral("(copy)")));

        // Delete asks first; the confirmed delete moves to a neighbour.
        QSignalSpy ask(&host, &NoteHost::deletePageRequested);
        host.actions()->trigger(QStringLiteral("notePageDelete"));
        QCOMPARE(ask.count(), 1);
        host.deletePageConfirmed();
        QCOMPARE(book->sectionById(firstSection)->pages.size(), 2);
        QVERIFY(host.currentPage() != nullptr);
        // The last section can never be deleted.
        host.deleteSectionConfirmed();
        QCOMPARE(book->sections().size(), 1);
        QVERIFY(!host.actions()->entry(QStringLiteral("noteSectionDelete"))->isEnabled());
    }

    void backgroundChoiceEditsThePage()
    {
        NoteHost host;
        host.actions()->trigger(QStringLiteral("notePageBackground_grid"));
        QCOMPARE(host.currentPage()->envelope.background, QStringLiteral("grid"));
        QVERIFY(host.actions()->entry(QStringLiteral("notePageBackground_grid"))->isChecked());
        host.setBackground(QStringLiteral("blank"));
        QCOMPARE(host.currentPage()->envelope.background, QStringLiteral("blank"));
    }

    // Edit > Find was a dead menu entry in QindaNote, and a search that
    // skipped pages nobody had opened would report "not found" for text
    // that is there.
    void findSelectsThePageHoldingTheText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/Search");
        QString error;
        QString wanted;
        {
            NoteDocument seed;
            QVERIFY2(seed.save(path, &error), qPrintable(error));
            Notebook *book = seed.notebook();
            NoteSection *section = book->sectionById(book->sections().first().id);
            NotePage &second = book->addPage(*section, QStringLiteral("Second"));
            second.envelope.blocksJson = R"({"blocks":[{"type":"text","text":"remember the pangolin"}]})";
            wanted = second.id;
            QVERIFY2(seed.save(path, &error), qPrintable(error));
        }
        NoteHost host;
        QVERIFY(host.loadPath(path));
        // The host lands on the FIRST page; the match is on a page that has
        // never been opened, so its envelope is empty in memory right now.
        QVERIFY(host.noteDocument()->notebook()->sections().first().pages.size() >= 2);
        QCOMPARE(host.currentPageId(), host.noteDocument()->notebook()->sections().first().pages.first().id);
        QVERIFY(host.actions()->entry(QStringLiteral("editFindAction"))->isEnabled());
        QVERIFY(host.findNext(QStringLiteral("pangolin"), false));
        QCOMPARE(host.currentPageId(), wanted);
        QVERIFY(!host.findNext(QStringLiteral("no such text"), false));
        // Replace All rewrites the typed text on every page.
        QCOMPARE(host.replaceAll(QStringLiteral("pangolin"), QStringLiteral("armadillo")), 1);
        QVERIFY(host.currentPage()->envelope.blocksJson.contains("armadillo"));
    }
};

NOTE_TEST_MAIN(QindaNoteTest)

#include "tst_qindanote.moc"
