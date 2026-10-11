// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_test_support.h"
#include "support/quick_dump.h"
#include "qindanote/src/note_versions.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;
namespace Ink = QindaOffice::Ink;

namespace {
QString s(const char *value) { return QString::fromUtf8(value); }
Ink::PageEnvelope envelope(const QString &title)
{
    Ink::PageEnvelope page;
    page.id = s("page-one");
    page.title = title;
    page.background = s("ruled");
    page.blocksJson = R"({"version":2,"pageColor":"#fff8d6","blocks":[{"type":"text","text":"Versioned notes","x":40,"y":50,"width":500}]})";
    Ink::Stroke stroke;
    stroke.samples = {{QPointF(40, 100), 0.5}, {QPointF(300, 180), 0.9}};
    stroke.complete = true;
    page.ink.appendStroke(stroke);
    return page;
}
QString firstId(const QVariantMap &result)
{
    return result.value(s("entries")).toList().first().toMap().value(s("id")).toString();
}
}

class NoteVersionsTest final : public QObject {
    Q_OBJECT
private slots:
    void capturesCoalesceDeduplicateAndRetainNewest()
    {
        QTemporaryDir dir;
        QString error;
        auto page = envelope(s("First"));
        const QDateTime start = QDateTime::fromString(s("2026-10-10T12:00:00Z"), Qt::ISODate);
        QVERIFY(Versions::capture(dir.path(), s("section"), page, false, &error, start));
        QVERIFY(Versions::capture(dir.path(), s("section"), page, true, &error, start.addSecs(1)));
        QCOMPARE(Versions::list(dir.path(), s("section"), page.id).size(), 1);
        page.title = s("Second");
        QVERIFY(Versions::capture(dir.path(), s("section"), page, false, &error, start.addSecs(299)));
        QCOMPARE(Versions::list(dir.path(), s("section"), page.id).size(), 1);
        QVERIFY(Versions::capture(dir.path(), s("section"), page, false, &error, start.addSecs(300)));
        for (int n = 0; n < 40; ++n) {
            page.title = QString::number(n);
            QVERIFY(Versions::capture(dir.path(), s("section"), page, true, &error, start.addSecs(301 + n)));
        }
        const auto entries = Versions::list(dir.path(), s("section"), page.id);
        QCOMPARE(entries.size(), Versions::kLimit);
        QCOMPARE(entries.first().title, s("39"));
        QCOMPARE(entries.last().title, s("8"));
        Ink::PageEnvelope restored;
        QVERIFY2(Versions::read(dir.path(), s("section"), page.id, entries.first().id, &restored, &error), qPrintable(error));
        QCOMPARE(Ink::pageToJson(restored), Ink::pageToJson(page));
    }

    void refusesUnsafeAndDamagedVersionsWithoutChangingOutput()
    {
        QTemporaryDir dir;
        QString error;
        auto page = envelope(s("Original"));
        QVERIFY(!Versions::capture(dir.path(), s("../escape"), page, true, &error));
        QVERIFY(Versions::capture(dir.path(), s("section"), page, true, &error));
        const QString id = Versions::list(dir.path(), s("section"), page.id).first().id;
        const QByteArray before = Ink::pageToJson(page);
        QVERIFY(!Versions::read(dir.path(), s("section"), page.id, s("../../escape"), &page, &error));
        QCOMPARE(Ink::pageToJson(page), before);
        QFile file(dir.filePath(s("sections/section/versions/page-one/") + id + s(".json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        data.insert(s("sha256"), s("wrong"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(data).toJson());
        file.close();
        QVERIFY(!Versions::read(dir.path(), s("section"), page.id, id, &page, &error));
        QCOMPARE(Ink::pageToJson(page), before);
        QVERIFY(Versions::remove(dir.path(), s("section"), page.id, id, &error));
        QVERIFY(Versions::list(dir.path(), s("section"), page.id).isEmpty());
    }

    void autosavePreservesPriorPageAndSaveAsCopiesHistoryAndAssets()
    {
        QTemporaryDir dir;
        NoteDocument doc;
        QString error;
        QVERIFY(doc.save(dir.filePath(s("original")), &error));
        Notebook *book = doc.notebook();
        const QString section = book->sections().first().id;
        const QString pageId = book->sections().first().pages.first().id;
        auto *page = book->pageById(section, pageId);
        const QByteArray initial = Ink::pageToJson(page->envelope);
        page->title = s("Edited");
        page->envelope.blocksJson = envelope(s("sample")).blocksJson;
        QVERIFY(book->save(&error));
        const auto entries = Versions::list(book->directory(), section, pageId);
        QCOMPARE(entries.size(), 1);
        Ink::PageEnvelope old;
        QVERIFY(Versions::read(book->directory(), section, pageId, entries.first().id, &old, &error));
        QCOMPARE(Ink::pageToJson(old), initial);
        QFile asset(QDir(book->directory()).filePath(s("assets/picture.bin")));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        asset.write("kept image bytes"); asset.close();
        QVERIFY2(doc.save(dir.filePath(s("copy")), &error), qPrintable(error));
        QCOMPARE(book->directory(), dir.filePath(s("copy")));
        QCOMPARE(Versions::list(book->directory(), section, pageId).size(), 1);
        QVERIFY(QFile::exists(QDir(book->directory()).filePath(s("assets/picture.bin"))));
        Notebook lazy;
        QVERIFY(lazy.load(book->directory(), &error));
        QVERIFY(lazy.save(&error));
        QCOMPARE(Versions::list(lazy.directory(), section, pageId).size(), 1);
        book->setVersionsEnabled(false);
        QVERIFY(book->save(&error));
        Notebook reopened;
        QVERIFY(reopened.load(book->directory(), &error));
        QVERIFY(!reopened.versionsEnabled());
    }

    void forcedCaptureDoesNotTrustADamagedDuplicate()
    {
        QTemporaryDir dir;
        QString error;
        const auto page = envelope(s("Current content"));
        QVERIFY(Versions::capture(dir.path(), s("section"), page, true, &error));
        const auto first = Versions::list(dir.path(), s("section"), page.id).first();
        QFile damaged(dir.filePath(s("sections/section/versions/page-one/") + first.id + s(".json")));
        QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        damaged.write("incomplete"); damaged.close();
        QVERIFY(Versions::capture(dir.path(), s("section"), page, true, &error));
        const auto newest = Versions::list(dir.path(), s("section"), page.id).first();
        QVERIFY(newest.id != first.id);
        Ink::PageEnvelope read;
        QVERIFY2(Versions::read(dir.path(), s("section"), page.id, newest.id, &read, &error), qPrintable(error));
        QCOMPARE(Ink::pageToJson(read), Ink::pageToJson(page));
    }

    void failedHistoryWritePreservesTheSavedPage()
    {
        QTemporaryDir dir;
        NoteDocument doc;
        QString error;
        QVERIFY(doc.save(dir.filePath(s("book")), &error));
        Notebook *book = doc.notebook();
        const QString section = book->sections().first().id;
        auto *page = book->pageById(section, book->sections().first().pages.first().id);
        const QString pagePath = QDir(book->directory()).filePath(s("sections/%1/pages/%2.json").arg(section, page->id));
        QFile saved(pagePath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray before = saved.readAll(); saved.close();
        QFile blocked(QDir(book->directory()).filePath(s("sections/%1/versions").arg(section)));
        QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("blocked"); blocked.close();
        page->title = s("Unsaved change");
        QVERIFY(!book->save(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), before);
        saved.close();
        // Disabling automatic retention remains a deliberate way to save a
        // page whose history destination is unavailable, without deleting it.
        book->setVersionsEnabled(false);
        QVERIFY(book->save(&error));
        QVERIFY(saved.remove());
        Notebook lazy;
        QVERIFY(lazy.load(book->directory(), &error));
        QVERIFY(!lazy.copyStructure(dir.filePath(s("copy")), &error));
        QCOMPARE(lazy.directory(), book->directory());
    }

    void restoreUsesOneHistoryStepAndKeepsTodaysContent_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1280;
        QTest::newRow("narrow") << 520;
    }
    void restoreUsesOneHistoryStepAndKeepsTodaysContent()
    {
        QFETCH(int, width);
        QTemporaryDir dir;
        Scene scene(QSize(width, width == 520 ? 720 : 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QString error;
        QVERIFY(scene.host.noteDocument()->save(dir.filePath(s("notebook")), &error));
        auto original = envelope(s("Earlier meeting notes"));
        original.id = scene.host.currentPageId();
        QVERIFY(scene.page->restoreEnvelope(original));
        QVERIFY(scene.host.savePageVersion().value(s("ok")).toBool());
        const QString version = firstId(scene.host.pageVersions());
        auto current = original;
        current.title = s("Current meeting notes");
        current.background = s("grid");
        current.ink.clear();
        current.blocksJson = R"({"blocks":[{"type":"text","text":"Current content"}]})";
        QVERIFY(scene.page->restoreEnvelope(current));
        const int history = scene.page->historyIndex();
        QVERIFY(scene.host.actions()->trigger(s("notePageVersionsAction")));
        QTest::qWait(300);
        auto *image = findItem(scene.window->contentItem(), s("noteVersionPreview"));
        QVERIFY(image && image->isVisible());
        QindaOffice::Test::saveGrab(scene.window, s("qindanote_versions_%1").arg(width));
        auto *restore = findItem(scene.window->contentItem(), s("noteVersionRestore"));
        QVERIFY(restore && restore->isVisible());
        const QPoint point = restore->mapToScene(QPointF(restore->width() / 2, restore->height() / 2)).toPoint();
        QVERIFY(QRect(QPoint(), scene.window->size()).contains(point));
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, point);
        QTRY_COMPARE(scene.page->historyIndex(), history + 1);
        QCOMPARE(Ink::pageToJson(scene.host.currentPage()->envelope), Ink::pageToJson(original));
        const auto rows = scene.host.pageVersions().value(s("entries")).toList();
        QCOMPARE(rows.size(), 2);
        QVERIFY(firstId(scene.host.pageVersions()) != version);
        scene.page->undo();
        QCOMPARE(Ink::pageToJson(scene.host.currentPage()->envelope), Ink::pageToJson(current));
        scene.page->redo();
        QCOMPARE(Ink::pageToJson(scene.host.currentPage()->envelope), Ink::pageToJson(original));
    }
};
NOTE_TEST_MAIN(NoteVersionsTest)
#include "tst_note_versions.moc"
