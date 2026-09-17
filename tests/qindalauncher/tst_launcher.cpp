// SPDX-License-Identifier: GPL-3.0-or-later
// The QindaQt Office launcher.
//
// It is the suite's front door, so the things worth pinning are the ones a
// front door gets wrong: sending a document to the wrong app, offering a
// file that is no longer there, reading another app's state destructively,
// or presenting a card that looks clickable for an app that is not there.

#include "qindalauncher/src/app_card.h"
#include "qindalauncher/src/launcher_catalog.h"
#include "qindalauncher/src/launcher_window.h"

#include "paths.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Launcher;

class LauncherTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void catalogNamesFourDistinctApps();
    void pathsGoToTheAppThatOpensThem();
    void recentsMergeTheFourListsAndSkipMissingFiles();
    void recentsSurviveAnUnreadableList();
    void windowShowsACardPerAppAndAnEmptyState();

private:
    // Both directories outlive every slot: a QTemporaryDir scoped to one
    // case deletes its files before the next case reads them, and the cases
    // here deliberately share one set of recents.
    QTemporaryDir m_config;
    QTemporaryDir m_documents;
    QString m_present;
};

void LauncherTest::initTestCase()
{
    QVERIFY(m_config.isValid());
    QVERIFY(m_documents.isValid());
    // AGENT-GUARD: redirect XDG_CONFIG_HOME before anything reads it — the
    // launcher walks the real per-app config dirs, and a test must never
    // read or write the user's own recents.
    qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());

    m_present = m_documents.filePath(QStringLiteral("report.odt"));
    QFile file(m_present);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    const auto writeRecents = [](const QString &appId, const QStringList &paths) {
        const QString dir = QindaOffice::Core::configDirFor(appId);
        QFile out(dir + QStringLiteral("/recent.json"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(QJsonArray::fromStringList(paths)).toJson());
    };
    writeRecents(QStringLiteral("QindaWrite"), {m_present});
    // A path that no longer exists, and a file that is not JSON at all.
    writeRecents(QStringLiteral("QindaCalc"),
                 {m_documents.filePath(QStringLiteral("gone.xlsx"))});
    const QString showDir = QindaOffice::Core::configDirFor(QStringLiteral("QindaShow"));
    QFile broken(showDir + QStringLiteral("/recent.json"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("this is not json");
}

void LauncherTest::catalogNamesFourDistinctApps()
{
    const QList<AppEntry> apps = officeApps();
    QCOMPARE(apps.size(), 4);
    QSet<QString> ids;
    QSet<QString> binaries;
    for (const AppEntry &app : apps) {
        QVERIFY2(!app.role.isEmpty(), qPrintable(app.id));
        QVERIFY2(!app.blurb.isEmpty(), qPrintable(app.id));
        QVERIFY2(app.desktopId.startsWith(QStringLiteral("org.qindaqt.")), qPrintable(app.id));
        ids.insert(app.id);
        binaries.insert(app.binary);
    }
    QCOMPARE(ids.size(), 4);
    QCOMPARE(binaries.size(), 4);
    QVERIFY(!openFilter().isEmpty());
}

void LauncherTest::pathsGoToTheAppThatOpensThem()
{
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/a.odt")), QStringLiteral("QindaWrite"));
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/a.DOCX")), QStringLiteral("QindaWrite"));
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/a.xlsx")), QStringLiteral("QindaCalc"));
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/a.csv")), QStringLiteral("QindaCalc"));
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/a.fodp")), QStringLiteral("QindaShow"));
    // A notebook is a directory with no suffix at all.
    QCOMPARE(appIdForPath(QStringLiteral("/tmp/Work")), QStringLiteral("QindaNote"));
}

void LauncherTest::recentsMergeTheFourListsAndSkipMissingFiles()
{
    const QList<RecentEntry> recents = recentDocuments();
    // Offering a document that is gone is worse than a shorter list.
    QCOMPARE(recents.size(), 1);
    QCOMPARE(recents.first().fileName, QStringLiteral("report.odt"));
    QCOMPARE(recents.first().appId, QStringLiteral("QindaWrite"));
    QCOMPARE(recents.first().folder, QFileInfo(m_present).absolutePath());
}

void LauncherTest::recentsSurviveAnUnreadableList()
{
    const QString dir = QindaOffice::Core::configDirFor(QStringLiteral("QindaShow"));
    // A file the launcher cannot parse is another app's state: skipped,
    // never rewritten, and never a reason to show nothing at all.
    QCOMPARE(recentDocuments().size(), 1);
    QCOMPARE(QFile(dir + QStringLiteral("/recent.json")).size(), qint64(16));
}

void LauncherTest::windowShowsACardPerAppAndAnEmptyState()
{
    LauncherWindow window;
    window.show();
    const QList<AppCard *> cards = window.findChildren<AppCard *>();
    QCOMPARE(cards.size(), 4);
    for (AppCard *card : cards) {
        // A painted card with no accessible identity is invisible to a
        // screen reader.
        QVERIFY2(!card->accessibleName().isEmpty(), qPrintable(card->objectName()));
        QVERIFY(!card->accessibleDescription().isEmpty());
    }
    QVERIFY(window.recentList() != nullptr);
    // One real recent from the earlier case; the empty-state row only shows
    // when there is nothing at all.
    QCOMPARE(window.recentList()->count(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaOffice"));
    LauncherTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_launcher.moc"
