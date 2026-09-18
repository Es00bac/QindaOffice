// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_model.h"
#include "menu_tree.h"
#include "standard_actions.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QindaOffice::Quick;

// The action registry every route (menu, strip, keyboard, panel) triggers
// through, and the menu tree both the local bar and the export read.
class ActionModelTest final : public QObject {
    Q_OBJECT
private slots:
    void entriesKeepStableIdsAndStripMnemonics()
    {
        ActionModel model;
        ActionEntry *entry = model.add(QStringLiteral("notePenTool"), QStringLiteral("&Pen"),
                                       QKeySequence(QStringLiteral("P")),
                                       QStringLiteral("pen-tool"));
        QCOMPARE(entry->id(), QStringLiteral("notePenTool"));
        QCOMPARE(entry->plainText(), QStringLiteral("Pen"));
        QCOMPARE(entry->shortcutText(), QStringLiteral("P"));
        QCOMPARE(model.entry(QStringLiteral("notePenTool")), entry);
        // Adding the same id again returns the existing entry, never a twin.
        QCOMPARE(model.add(QStringLiteral("notePenTool"), QStringLiteral("Other")), entry);
        QCOMPARE(entry->text(), QStringLiteral("&Pen"));
        entry->setText(QStringLiteral("Save && Close"));
        QCOMPARE(entry->plainText(), QStringLiteral("Save & Close"));
    }

    void radioGroupKeepsOneChecked()
    {
        ActionModel model;
        ActionEntry *pen = model.add(QStringLiteral("pen"), QStringLiteral("Pen"));
        ActionEntry *eraser = model.add(QStringLiteral("eraser"), QStringLiteral("Eraser"));
        pen->setRadioGroup(QStringLiteral("tool"));
        eraser->setRadioGroup(QStringLiteral("tool"));
        QVERIFY(pen->isCheckable());
        pen->setChecked(true);
        QVERIFY(pen->isChecked());
        eraser->trigger();
        QVERIFY(eraser->isChecked());
        QVERIFY(!pen->isChecked());
        // Triggering the checked radio member again keeps it checked.
        eraser->trigger();
        QVERIFY(eraser->isChecked());
    }

    void disabledEntriesAreInertFromEveryRoute()
    {
        ActionModel model;
        ActionEntry *save = model.add(QStringLiteral("fileSaveAction"), QStringLiteral("Save"));
        QSignalSpy triggered(&model, &ActionModel::triggered);
        save->setEnabled(false);
        QVERIFY(model.trigger(QStringLiteral("fileSaveAction")));
        QCOMPARE(triggered.count(), 0);
        save->setEnabled(true);
        QVERIFY(model.trigger(QStringLiteral("fileSaveAction")));
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("fileSaveAction"));
        QVERIFY(!model.trigger(QStringLiteral("nope")));
    }

    void changesAreReportedOncePerField()
    {
        ActionModel model;
        ActionEntry *entry = model.add(QStringLiteral("x"), QStringLiteral("X"));
        QSignalSpy changed(&model, &ActionModel::entryChanged);
        entry->setEnabled(false);
        entry->setEnabled(false); // no change, no signal
        entry->setCheckable(true);
        entry->setChecked(true);
        QCOMPARE(changed.count(), 3);
    }

    void standardSetIsHonestlyDisabledUntilWired()
    {
        ActionModel model;
        addStandardActions(&model, QStringLiteral("QindaTest"));
        QVERIFY(model.entry(QString::fromLatin1(ActionId::kFileNew))->isEnabled());
        QVERIFY(!model.entry(QString::fromLatin1(ActionId::kFileSave))->isEnabled());
        QVERIFY(!model.entry(QString::fromLatin1(ActionId::kEditUndo))->isEnabled());
        QCOMPARE(model.entry(QString::fromLatin1(ActionId::kFileCloseWindow))->shortcutText(),
                 QStringLiteral("Ctrl+W"));
        QCOMPARE(model.entry(QString::fromLatin1(ActionId::kFileQuit))->shortcutText(),
                 QStringLiteral("Ctrl+Q"));
        QVERIFY(model.entry(QString::fromLatin1(ActionId::kViewShowMenuBar))->isChecked());
    }

    void menuTreeInsertsRelativeToAnchors()
    {
        MenuTree tree;
        tree.setTopLevel(standardMenuTree());
        QSignalSpy changed(&tree, &MenuTree::menusChanged);
        QVERIFY(tree.insertMenuBefore(QStringLiteral("aiMenu"),
                                      MenuNode::submenu(QStringLiteral("noteInkMenu"),
                                                        QStringLiteral("&Ink"))));
        QStringList order;
        for (const MenuNode &menu : tree.topLevel()) {
            order.append(menu.id);
        }
        QCOMPARE(order, QStringList({QStringLiteral("fileMenu"), QStringLiteral("editMenu"),
                                     QStringLiteral("viewMenu"), QStringLiteral("noteInkMenu"),
                                     QStringLiteral("aiMenu"), QStringLiteral("settingsMenu"),
                                     QStringLiteral("helpMenu")}));
        // App File entries go BEFORE Close Window (never under Quit).
        QVERIFY(tree.insertInto(QStringLiteral("fileMenu"),
                                QString::fromLatin1(ActionId::kFileCloseWindow),
                                MenuNode::action(QStringLiteral("noteExportPdf"))));
        const MenuNode *file = tree.find(QStringLiteral("fileMenu"));
        QVERIFY(file != nullptr);
        int exportAt = -1;
        int closeAt = -1;
        for (int i = 0; i < file->children.size(); ++i) {
            if (file->children.at(i).id == QLatin1String("noteExportPdf")) {
                exportAt = i;
            }
            if (file->children.at(i).id == QLatin1String(ActionId::kFileCloseWindow)) {
                closeAt = i;
            }
        }
        QVERIFY(exportAt >= 0 && closeAt == exportAt + 1);
        // Open Recent is replaceable in place.
        QVERIFY(tree.replaceSubmenu(QStringLiteral("fileRecentMenu"),
                                    MenuNode::submenu(QStringLiteral("fileRecentMenu"),
                                                      QStringLiteral("Open &Recent"),
                                                      {MenuNode::action(QStringLiteral("fileRecentItem0"))})));
        QCOMPARE(tree.find(QStringLiteral("fileRecentMenu"))->children.size(), 1);
        QCOMPARE(changed.count(), 3);
        const QVariantMap first = tree.menus().first().toMap();
        QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("menu"));
        QCOMPARE(first.value(QStringLiteral("id")).toString(), QStringLiteral("fileMenu"));
    }
};

// A QGuiApplication (offscreen), not a QCoreApplication: QKeySequence's
// standard keys resolve through the platform theme.
QTEST_MAIN(ActionModelTest)
#include "tst_action_model.moc"
