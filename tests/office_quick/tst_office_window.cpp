// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_appearance.h"
#include "document_host.h"
#include "standard_actions.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Quick;

// The real window QML over a real host, offscreen: the static module
// registers, the bands exist, the menu bar is built from the tree with
// live-bound items, the ribbon draws the same entries (compact mode, the
// command palette), and a panel-style activation reaches the host.
class OfficeWindowTest final : public QObject {
    Q_OBJECT

    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
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

private slots:
    void hostTeardownDestroysWindowBeforeBindingsLoseHost()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        auto *host = new DocumentHost;
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaOffice.Shell
OfficeWindow { width: 400; height: 300
    Rectangle { objectName: "content" }
}
)qml", QUrl(QStringLiteral("teardown.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("host"), QVariant::fromValue(host)}});
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        host->attachWindow(window);
        QPointer<QQuickWindow> observed(window);
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Cannot read property .* of null")));
        delete host;
        QVERIFY(observed.isNull());
    }

    void windowBuildsFromTheHost()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        DocumentHost host;
        host.menuTree()->insertMenuBefore(
            QStringLiteral("aiMenu"),
            MenuNode::submenu(QStringLiteral("testMenu"), QStringLiteral("&Test"),
                              {MenuNode::action(QString::fromLatin1(ActionId::kFileNew))}));
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
OfficeWindow {
    width: 800; height: 600
    ribbonTabs: [{ id: "home", label: "Home", groups: [
        { id: "g", label: "Group", items: [{ action: "fileNewAction", size: "large" }, "fileOpenAction",
                                           "editUndoAction", "viewShowMenuBarAction", "noSuchCommand"] }] }]
    Rectangle { objectName: "content"; color: "white" }
}
)qml", QUrl(QStringLiteral("inline.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("host"), QVariant::fromValue(&host)}});
        QVERIFY2(object != nullptr, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY(window != nullptr);
        host.attachWindow(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *root = window->contentItem();

        // Bands and the menu bar from the tree (no export offscreen, so the
        // local bar stays visible: fail-closed). AGENT-NOTE: popups and the
        // menu bar are QObject children of the WINDOW, not of its content
        // item — look them up from the window.
        QVERIFY(host.localMenuVisible());
        QQuickItem *menuBar = findItem(root, QStringLiteral("officeMenuBar"));
        QVERIFY(menuBar != nullptr);
        QVERIFY(menuBar->isVisible());
        QObject *menuObject = nullptr;
        QTRY_VERIFY((menuObject = window->findChild<QObject *>(QStringLiteral("menu_testMenu"))) != nullptr);
        QVERIFY(menuObject != nullptr);
        QCOMPARE(menuObject->property("title").toString(), QStringLiteral("&Test"));
        QObject *newItem = menuObject->findChild<QObject *>(QStringLiteral("menuItem_fileNewAction"));
        QVERIFY(newItem != nullptr);
        QCOMPARE(newItem->property("text").toString(), QStringLiteral("&New"));
        QCOMPARE(newItem->property("shortcut").toString(), QStringLiteral("Ctrl+N"));

        // D-041: a ribbon button follows its entry; an unknown id is skipped
        // (one warning), never drawn as a dead button.
        QQuickItem *ribbon = findItem(root, QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr && ribbon->isVisible());
        QCOMPARE(ribbon->property("currentTabId").toString(), QStringLiteral("home"));
        QQuickItem *undoButton = findItem(root, QStringLiteral("ribbonAction_editUndoAction"));
        QVERIFY(undoButton != nullptr);
        QVERIFY(!undoButton->isEnabled());
        host.actions()->setEnabled(QString::fromLatin1(ActionId::kEditUndo), true);
        QTRY_VERIFY(undoButton->isEnabled());
        QVERIFY(findItem(root, QStringLiteral("ribbonItem_noSuchCommand")) == nullptr);
        QCOMPARE(findItem(root, QStringLiteral("ribbonAction_fileNewAction"))->property("size").toString(),
                 QStringLiteral("large"));
        // View ▸ Compact Ribbon is the ribbon's mode.
        host.actions()->trigger(QString::fromLatin1(ActionId::kViewCompactRibbon));
        QTRY_COMPARE(ribbon->property("mode").toString(), QStringLiteral("compact"));
        host.actions()->trigger(QString::fromLatin1(ActionId::kViewCompactRibbon));
        QTRY_COMPARE(ribbon->property("mode").toString(), QStringLiteral("expanded"));

        // Help ▸ Search Commands… (Alt+Q) opens the palette over the menu
        // tree's commands; activating a row triggers the entry by id.
        QObject *palette = window->findChild<QObject *>(QStringLiteral("commandPalette"));
        QVERIFY(palette != nullptr);
        QVERIFY(host.actions()->trigger(QString::fromLatin1(ActionId::kHelpSearchCommands)));
        QTRY_VERIFY(palette->property("opened").toBool());
        const QVariantList commands = palette->property("commands").toList();
        QVERIFY(commands.size() > 20);
        bool hasNew = false;
        for (const QVariant &row : commands) {
            const QVariantMap map = row.toMap();
            if (map.value(QStringLiteral("id")).toString() == QString::fromLatin1(ActionId::kFileNew)) {
                hasNew = true;
                QCOMPARE(map.value(QStringLiteral("label")).toString(), QStringLiteral("New"));
                QCOMPARE(map.value(QStringLiteral("section")).toString(), QStringLiteral("File"));
            }
        }
        QVERIFY(hasNew);
        QSignalSpy fromPalette(&host, &DocumentHost::newWindowRequested);
        QMetaObject::invokeMethod(palette, "activated", Q_ARG(QString, QString::fromLatin1(ActionId::kFileNew)));
        QCOMPARE(fromPalette.count(), 1);
        QMetaObject::invokeMethod(palette, "close");
        QVERIFY(findItem(root, QStringLiteral("content")) != nullptr);
        QVERIFY(findItem(root, QStringLiteral("officeStatusBar")) != nullptr);

        // The panel's route: trigger by id reaches the host's handler.
        QSignalSpy requested(&host, &DocumentHost::newWindowRequested);
        QVERIFY(host.actions()->trigger(QString::fromLatin1(ActionId::kFileNew)));
        QCOMPARE(requested.count(), 1);

        // View ▸ Show Menu Bar hides the local bar.
        host.actions()->trigger(QString::fromLatin1(ActionId::kViewShowMenuBar));
        QTRY_VERIFY(!host.localMenuVisible());
        QTRY_VERIFY(!menuBar->isVisible());
        delete window;
    }

    // Off the desktop the window is on the Preferences preset; a published
    // desktop theme (here: one installed theme by id, the seam
    // app_runner's desktop lookup ends in) wins over it.
    void desktopThemeWinsOverThePreset()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        DocumentHost host;
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
OfficeWindow { width: 400; height: 300; readonly property string themeName: Tk.Theme.name
    Rectangle { objectName: "content" } }
)qml", QUrl(QStringLiteral("inline.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *window = component.createWithInitialProperties(
            {{QStringLiteral("host"), QVariant::fromValue(&host)}});
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        QCOMPARE(window->property("tokensApplied").toBool(), false);
        const QString preset = window->property("themeName").toString();
        QVERIFY(!preset.isEmpty());

        QString error;
        QVERIFY2(DesktopAppearance::publishTheme(engine, QStringLiteral("qinda-dark"), false, &error),
                 qPrintable(error));
        QTRY_VERIFY(window->property("tokensApplied").toBool());
        QCOMPARE(window->property("themeName").toString(), QStringLiteral("qinda-dark"));
        // An unknown theme id is refused, never mapped to another theme.
        QVERIFY(!DesktopAppearance::publishTheme(engine, QStringLiteral("no-such-theme"), false, &error));
        QVERIFY(error.contains(QStringLiteral("no-such-theme")));
        delete window;
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_STATE_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaTest"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaTest"));
    OfficeWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_office_window.moc"
