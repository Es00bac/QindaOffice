// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"
#include "standard_actions.h"

#include <QApplication>
#include <QMenuBar>
#include <QtTest>

using namespace QindaOffice::Shell;

// Verifies the shared document-window frame: standard menu order
// (File/Edit/View/AI/Settings/Help), the View menu-bar action, and the
// fail-closed global-menu behaviour under the offscreen platform.
class DocumentWindowTest : public QObject {
    Q_OBJECT
private slots:
    void menuOrderFollowsShellSpec();
    void viewActionTogglesLocalMenuBar();
    void offscreenExportFailsClosedAndKeepsLocalBar();
    void actionLookupByObjectName();
    void cleanupTestCase();
};

static QStringList menuTitles(DocumentWindow *window)
{
    QStringList titles;
    const QList<QAction *> actions = window->menuBar()->actions();
    for (QAction *action : actions) {
        titles.append(action->text());
    }
    return titles;
}

void DocumentWindowTest::menuOrderFollowsShellSpec()
{
    DocumentWindow window;
    const QStringList titles = menuTitles(&window);
    QCOMPARE(titles, QStringList({"&File", "&Edit", "&View", "&AI", "&Settings", "&Help"}));
}

void DocumentWindowTest::viewActionTogglesLocalMenuBar()
{
    DocumentWindow window;
    QAction *toggle = window.action(QString::fromLatin1(ActionId::kViewShowMenuBar));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isChecked());
    toggle->setChecked(false);
    QVERIFY(!window.menuBar()->isVisibleTo(&window));
    toggle->setChecked(true);
    QVERIFY(window.menuBar()->isVisibleTo(&window));
}

void DocumentWindowTest::offscreenExportFailsClosedAndKeepsLocalBar()
{
    // Under the offscreen platform there is no Wayland/xcb announcement, so
    // start() must fail closed: no export, local menu bar stays visible.
    DocumentWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.menuBar()->isVisibleTo(&window));
}

void DocumentWindowTest::actionLookupByObjectName()
{
    DocumentWindow window;
    QCOMPARE(window.action(QString::fromLatin1(ActionId::kFileNew)),
             window.standardActions()->fileNew);
    QVERIFY(window.action(QStringLiteral("noSuchAction")) == nullptr);
}

void DocumentWindowTest::cleanupTestCase()
{
    // Windows created in tests were stack-owned; nothing leaks by design.
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    DocumentWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_document_window.moc"
