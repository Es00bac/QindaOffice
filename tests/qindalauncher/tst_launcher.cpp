// SPDX-License-Identifier: GPL-3.0-or-later
// The QindaQt Office launcher.
//
// It is the suite's front door, so the things worth pinning are the ones a
// front door gets wrong: sending a document to the wrong app, offering a
// file that is no longer there, reading another app's state destructively,
// or presenting a card that looks clickable for an app that is not there.

#include "qindalauncher/src/launcher_catalog.h"
#include "qindalauncher/src/launcher_controller.h"

#include "paths.h"
#include "support/quick_dump.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Launcher;

namespace {

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    for (QQuickItem *child : parent->childItems()) {
        if (child->objectName() == name) {
            return child;
        }
        if (QQuickItem *found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

class LauncherTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void catalogNamesFourDistinctApps();
    void pathsGoToTheAppThatOpensThem();
    void recentsMergeTheFourListsAndSkipMissingFiles();
    void recentsSurviveAnUnreadableList();
    void controllerPublishesAppsAndRecents();
    void windowShowsACardPerAppAndTheRecentList();

private:
    // The documents directory outlives every slot: a QTemporaryDir scoped
    // to one case deletes its files before the next case reads them, and
    // the cases here deliberately share one set of recents.
    QTemporaryDir m_documents;
    QString m_present;
};

void LauncherTest::initTestCase()
{
    QVERIFY(m_documents.isValid());
    m_present = m_documents.filePath(QStringLiteral("report.odt"));
    QFile file(m_present);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    // AGENT-GUARD: XDG_CONFIG_HOME is redirected in main(), before anything
    // reads it — the launcher walks the real per-app config dirs, and a test
    // must never read or write the user's own recents.
    const auto writeRecents = [](const QString &appId, const QStringList &paths) {
        const QString dir = QindaOffice::Core::configDirFor(appId);
        QFile out(dir + QStringLiteral("/recent.json"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(QJsonArray::fromStringList(paths)).toJson());
    };
    writeRecents(QStringLiteral("QindaWrite"), {m_present});
    // A path that no longer exists, and a file that is not JSON at all.
    writeRecents(QStringLiteral("QindaCalc"), {m_documents.filePath(QStringLiteral("gone.xlsx"))});
    const QString showDir = QindaOffice::Core::configDirFor(QStringLiteral("QindaShow"));
    QFile broken(showDir + QStringLiteral("/recent.json"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("this is not json");
}

void LauncherTest::catalogNamesFourDistinctApps()
{
    const QList<AppEntry> apps = officeApps();
    // Write, Calc, Show, Note, Mail (M44), Books (M47), Base (M49),
    // Diagram (M50) and Plan (M51).
    QVERIFY(apps.size() >= 5);
    QSet<QString> ids;
    QSet<QString> binaries;
    for (const AppEntry &app : apps) {
        QVERIFY2(!app.role.isEmpty(), qPrintable(app.id));
        QVERIFY2(!app.blurb.isEmpty(), qPrintable(app.id));
        QVERIFY2(app.desktopId.startsWith(QStringLiteral("org.qindaqt.")), qPrintable(app.id));
        ids.insert(app.id);
        binaries.insert(app.binary);
    }
    QCOMPARE(ids.size(), apps.size());
    QCOMPARE(binaries.size(), apps.size());
    QVERIFY(openNameFilters().size() >= 4);
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

void LauncherTest::controllerPublishesAppsAndRecents()
{
    LauncherController controller;
    const QVariantList apps = controller.apps();
    QCOMPARE(apps.size(), officeApps().size());
    for (const QVariant &value : apps) {
        const QVariantMap app = value.toMap();
        QVERIFY(!app.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(!app.value(QStringLiteral("role")).toString().isEmpty());
        QVERIFY(!app.value(QStringLiteral("blurb")).toString().isEmpty());
        // The window needs to know whether the binary is there at all.
        QVERIFY(app.contains(QStringLiteral("available")));
        QVERIFY(app.contains(QStringLiteral("iconSource")));
    }
    QCOMPARE(controller.recents().size(), 1);
    QCOMPARE(controller.recents().first().toMap().value(QStringLiteral("fileName")).toString(),
             QStringLiteral("report.odt"));
    // Opening a missing app fails visibly rather than silently.
    QVERIFY(controller.status().isEmpty());
}

void LauncherTest::windowShowsACardPerAppAndTheRecentList()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
    LauncherController controller;
    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Launcher/LauncherWindow.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("launcher"), QVariant::fromValue(&controller)}}));
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window != nullptr);
    window->resize(1100, 760);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(100);

    for (const AppEntry &app : officeApps()) {
        QQuickItem *card = findItem(window->contentItem(), QStringLiteral("appCard_") + app.id);
        QVERIFY2(card != nullptr, qPrintable(app.id));
        // A card with no accessible identity is invisible to a screen
        // reader, so ask for what one would actually read.
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(card);
        QVERIFY2(iface != nullptr, qPrintable(app.id));
        QVERIFY2(!iface->text(QAccessible::Name).isEmpty(), qPrintable(app.id));
    }
    // One real recent from the earlier case; the empty state only shows
    // when there is nothing at all.
    QVERIFY(findItem(window->contentItem(), QStringLiteral("recentRow_0")) != nullptr);
    QQuickItem *empty = findItem(window->contentItem(), QStringLiteral("launcherRecentEmpty"));
    QVERIFY(empty != nullptr && !empty->isVisible());
    QVERIFY(findItem(window->contentItem(), QStringLiteral("launcherOpenButton")) != nullptr);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindaoffice_launcher"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaOffice"));
    LauncherTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_launcher.moc"
