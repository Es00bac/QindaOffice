// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_model.h"
#include "document_host.h"
#include "menu_source.h"
#include "menu_tree.h"
#include "standard_actions.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QGuiApplication>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

using namespace QindaOffice::Quick;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

// The panel export's contract, offscreen and without D-Bus: the snapshot of
// a window's menu is complete, the desktop's validator accepts it, and every
// exported command resolves and fires in the ActionModel — the class of
// defect the Widgets suite shipped (plan V5: 71 of 72 panel commands dead).
class MenuExportTest final : public QObject {
    Q_OBJECT

    static void collectActions(const Protocol::MenuItem &item, QStringList &ids)
    {
        if (item.kind == Protocol::MenuItemKind::Action) {
            ids.append(item.id);
        }
        for (const Protocol::MenuItem &child : item.children) {
            collectActions(child, ids);
        }
    }

private slots:
    void standardWindowExportsACompleteValidTree()
    {
        DocumentHost host;
        // An app-shaped addition: an Ink menu with radio tools and a colour
        // submenu, inserted before AI like every app menu.
        ActionModel *actions = host.actions();
        for (const char *id : {"notePenTool", "noteHighlighterTool", "noteEraserTool"}) {
            ActionEntry *entry = actions->add(QString::fromLatin1(id), QString::fromLatin1(id));
            entry->setRadioGroup(QStringLiteral("noteTool"));
        }
        actions->entry(QStringLiteral("notePenTool"))->setChecked(true);
        MenuNode ink = MenuNode::submenu(
            QStringLiteral("noteInkMenu"), QStringLiteral("&Ink"),
            {MenuNode::action(QStringLiteral("notePenTool")),
             MenuNode::action(QStringLiteral("noteHighlighterTool")),
             MenuNode::action(QStringLiteral("noteEraserTool")), MenuNode::separator(),
             MenuNode::submenu(QStringLiteral("noteInkPenColorMenu"), QStringLiteral("Pen &Colour"),
                               {MenuNode::action(QStringLiteral("notePenTool"))})});
        // The same action twice in one tree is a duplicate id — see below.
        ink.children.last().children.clear();
        actions->add(QStringLiteral("notePenColor_black"), QStringLiteral("Black"))
            ->setRadioGroup(QStringLiteral("notePenColor"));
        ink.children.last().children.append(MenuNode::action(QStringLiteral("notePenColor_black")));
        host.menuTree()->insertMenuBefore(QStringLiteral("aiMenu"), ink);

        ActionModelMenuSource source(host.menuTree(), actions, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY2(snapshot.complete, qPrintable(snapshot.defectCode));
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY2(result.accepted,
                 qPrintable(QStringLiteral("%1 at %2").arg(result.reasonCode, result.path)));
        QCOMPARE(snapshot.tree.items.size(), 7); // File Edit View Ink AI Settings Help

        // Every exported action resolves and fires.
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY(ids.size() > 30);
        QSignalSpy triggered(actions, &ActionModel::triggered);
        int live = 0;
        for (const QString &id : ids) {
            ActionEntry *entry = actions->entry(id);
            QVERIFY2(entry != nullptr, qPrintable(id));
            if (entry->isEnabled()) {
                ++live;
            }
            QVERIFY2(actions->trigger(id), qPrintable(id));
        }
        QCOMPARE(triggered.count(), live);

        // Mnemonics leave the text and land in the index; radio groups and
        // the checked tool survive; separators carry unique ids.
        const Protocol::MenuItem &inkItem = snapshot.tree.items.at(3);
        QCOMPARE(inkItem.text, QStringLiteral("Ink"));
        QCOMPARE(inkItem.mnemonicIndex, 0);
        QCOMPARE(inkItem.children.at(0).radioGroup, QStringLiteral("noteTool"));
        QVERIFY(inkItem.children.at(0).checked);
        QVERIFY(!inkItem.children.at(1).checked);
        QCOMPARE(inkItem.children.at(3).kind, Protocol::MenuItemKind::Separator);
    }

    void unknownCommandInTheTreeFailsClosed()
    {
        DocumentHost host;
        host.menuTree()->appendTo(QStringLiteral("editMenu"),
                                  MenuNode::action(QStringLiteral("neverCreated")));
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY(!snapshot.complete);
        QVERIFY(snapshot.defectCode.startsWith(QStringLiteral("unknown-action:")));
        QVERIFY(snapshot.tree.items.isEmpty());
    }

    void duplicateActionInTheTreeIsRejectedByTheValidator()
    {
        DocumentHost host;
        host.menuTree()->appendTo(QStringLiteral("editMenu"),
                                  MenuNode::action(QString::fromLatin1(ActionId::kFileSave)));
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY(snapshot.complete);
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY(!result.accepted);
        QCOMPARE(result.reasonCode, QStringLiteral("duplicate-id"));
    }

    void twoCheckedInOneRadioGroupCannotHappen()
    {
        ActionModel model;
        ActionEntry *a = model.add(QStringLiteral("a"), QStringLiteral("A"));
        ActionEntry *b = model.add(QStringLiteral("b"), QStringLiteral("B"));
        a->setRadioGroup(QStringLiteral("g"));
        b->setRadioGroup(QStringLiteral("g"));
        a->setChecked(true);
        b->setChecked(true);
        MenuTree tree;
        tree.setTopLevel({MenuNode::submenu(QStringLiteral("m"), QStringLiteral("M"),
                                            {MenuNode::action(QStringLiteral("a")),
                                             MenuNode::action(QStringLiteral("b"))})});
        ActionModelMenuSource source(&tree, &model, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY(snapshot.complete);
        QVERIFY(Protocol::validateMenuTree(snapshot.tree).accepted);
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaTest"));
    MenuExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_menu_export.moc"
