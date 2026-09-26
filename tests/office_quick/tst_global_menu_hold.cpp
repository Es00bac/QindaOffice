// SPDX-License-Identifier: GPL-3.0-or-later
// The global menu export holds publication while its window is inactive:
// the panel dismisses an open menu on every changed tree, and opening that
// menu takes focus from the window — the moment the suite flips Paste
// (clipboard re-announced) or Undo (an inline editor commits). Regression
// for "the Office global menu closes the instant it opens".

#include "action_model.h"
#include "global_menu_export.h"
#include "menu_tree.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

using namespace QindaOffice::Quick;

class GlobalMenuHoldTest final : public QObject {
    Q_OBJECT
private slots:
    void changesWaitWhileTheWindowIsInactive()
    {
        QWindow window;
        window.resize(200, 120);
        window.show();
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QWindow panel; // stands in for the panel's menu taking focus
        panel.resize(80, 40);

        MenuTree tree;
        ActionModel actions;
        ActionEntry *paste = actions.add(QStringLiteral("editPasteAction"), QStringLiteral("Paste"));
        GlobalMenuExport exporter(&tree, &actions, &window);

        // Active: a change publishes after the coalescing interval.
        paste->setEnabled(false);
        QTRY_COMPARE(exporter.refreshPasses(), 1);

        // Focus moves away (the panel menu opens); the app flips Paste.
        panel.show();
        panel.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QVERIFY(!window.isActive());
        paste->setEnabled(true);
        QTest::qWait(GlobalMenuExport::kCoalesceMs * 3);
        QCOMPARE(exporter.refreshPasses(), 1); // nothing reached the panel
        QVERIFY(exporter.isHoldingChanges());

        // Back in focus: the held change goes out once.
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_COMPARE(exporter.refreshPasses(), 2);
        QVERIFY(!exporter.isHoldingChanges());
        QTest::qWait(GlobalMenuExport::kCoalesceMs * 2);
        QCOMPARE(exporter.refreshPasses(), 2);
    }

    void aFocusOutRightAfterAChangeStillHoldsIt()
    {
        // The Wayland order: the clipboard change arrives, THEN the queued
        // focus-out. The coalescing interval must cover that gap.
        QWindow window;
        window.resize(200, 120);
        window.show();
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QWindow panel;
        panel.resize(80, 40);
        MenuTree tree;
        ActionModel actions;
        ActionEntry *paste = actions.add(QStringLiteral("editPasteAction"), QStringLiteral("Paste"));
        GlobalMenuExport exporter(&tree, &actions, &window);

        paste->setEnabled(false);
        panel.show();
        panel.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QTest::qWait(GlobalMenuExport::kCoalesceMs * 3);
        QCOMPARE(exporter.refreshPasses(), 0);
        QVERIFY(exporter.isHoldingChanges());
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    GlobalMenuHoldTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_global_menu_hold.moc"
