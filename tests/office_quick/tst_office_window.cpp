// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_host.h"
#include "standard_actions.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Quick;

// The real window QML over a real host, offscreen: the static module
// registers, the bands exist, the menu bar is built from the tree with
// live-bound items, and a panel-style activation reaches the host.
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
    void windowBuildsFromTheHost()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_DIR) + QStringLiteral("/.."));
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
    toolBars: [ ToolBarFromModel { objectName: "mainBar"; actions: host.actions
                commands: ["fileNewAction", "fileOpenAction", "|", "editUndoAction", "viewShowMenuBarAction"] } ]
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

        // A tool strip button follows its entry.
        QQuickItem *undoButton = findItem(root, QStringLiteral("toolButton_editUndoAction"));
        QVERIFY(undoButton != nullptr);
        QVERIFY(!undoButton->isEnabled());
        host.actions()->setEnabled(QString::fromLatin1(ActionId::kEditUndo), true);
        QTRY_VERIFY(undoButton->isEnabled());
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
