// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaDiagram host must pass the desktop's own
// validator, and EVERY exported command must resolve in the host's
// ActionModel and fire (the same gate as the other apps' menu-export tests).

#include "diagram_app_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QSignalSpy>
#include <QUuid>

using namespace DiagramAppTest;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModel;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class QindaDiagramMenuExportTest final : public QObject {
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
    void exportedTreeIsCompleteAndValid()
    {
        for (const bool selection : {false, true}) {
            DiagramHost host;
            if (selection) {
                seedFlowchart(host.diagram());
                host.diagram()->selectAll();
            }
            ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
            const auto snapshot = source.snapshot();
            QVERIFY2(snapshot.complete, qPrintable(QStringLiteral("incomplete: ") + snapshot.defectCode));
            const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
            QVERIFY2(result.accepted,
                     qPrintable(QStringLiteral("rejected: %1 at %2").arg(result.reasonCode, result.path)));
            QStringList titles;
            for (const Protocol::MenuItem &item : snapshot.tree.items) {
                titles.append(item.text);
            }
            QCOMPARE(titles, (QStringList{str("File"), str("Edit"), str("View"), str("Insert"), str("Format"),
                                          str("Arrange"), str("Design"), str("AI"), str("Settings"), str("Help")}));
        }
    }

    void everyExportedCommandResolvesAndFires()
    {
        DiagramHost host;
        seedFlowchart(host.diagram());
        seedNetwork(host.diagram());
        host.diagram()->selectAll();
        ActionModel *actions = host.actions();
        ActionModelMenuSource source(host.menuTree(), actions, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY2(ids.size() > 120, qPrintable(QString::number(ids.size())));
        // Every diagram command is in the tree exactly once.
        for (ActionEntry *e : actions->entries()) {
            if (e->id().startsWith(QStringLiteral("diagram"))) {
                QVERIFY2(ids.count(e->id()) == 1, qPrintable(e->id()));
            }
        }
        QSignalSpy triggered(actions, &ActionModel::triggered);
        QStringList unresolved;
        int fired = 0;
        for (const QString &id : ids) {
            ActionEntry *entry = actions->entry(id);
            if (entry == nullptr) {
                unresolved.append(id);
                continue;
            }
            // Commands that close, quit, open system dialogs or change the
            // page out from under the loop resolve but are not pulled here.
            if (!entry->isEnabled() || id.startsWith(QStringLiteral("file")) || id.startsWith(QStringLiteral("help"))
                || id.startsWith(QStringLiteral("settings")) || id.startsWith(QStringLiteral("ai"))
                || id == QStringLiteral("diagramDeletePageAction") || id == QStringLiteral("editCutAction")
                || id == QStringLiteral("diagramDeleteAction")) {
                continue;
            }
            const int before = triggered.count();
            QVERIFY2(actions->trigger(id), qPrintable(id));
            QCOMPARE(triggered.count(), before + 1);
            ++fired;
            host.diagram()->selectAll(); // some commands change the selection
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join(QLatin1Char(' '))));
        QVERIFY2(fired > 90, qPrintable(QString::number(fired)));
    }
};

DIAGRAM_APP_TEST_MAIN(QindaDiagramMenuExportTest)

#include "tst_qindadiagram_menu_export.moc"
