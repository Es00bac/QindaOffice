// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaBase host must pass the desktop's own
// validator, and EVERY exported command must resolve in the host's
// ActionModel and fire (the same gate as the other apps' menu-export tests).

#include "base_app_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QSignalSpy>
#include <QUuid>

using namespace BaseAppTest;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModel;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class QindaBaseMenuExportTest final : public QObject {
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
        for (const bool database : {false, true}) {
            QTemporaryDir dir;
            BaseHost host;
            if (database) {
                QString error;
                QVERIFY(host.baseDocument()->createAt(dir.filePath(str("db.qbase")), str("donors"), &error));
            }
            ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
            const auto snapshot = source.snapshot();
            QVERIFY2(snapshot.complete, qPrintable(str("incomplete: ") + snapshot.defectCode));
            const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
            QVERIFY2(result.accepted, qPrintable(str("rejected: %1 at %2").arg(result.reasonCode, result.path)));
            QStringList titles;
            for (const Protocol::MenuItem &item : snapshot.tree.items) {
                titles.append(item.text);
            }
            QCOMPARE(titles, (QStringList{str("File"), str("Edit"), str("View"), str("Records"), str("Create"),
                                          str("External Data"), str("Design"), str("Database Tools"), str("AI"),
                                          str("Settings"), str("Help")}));
        }
    }

    void everyExportedCommandResolvesAndFires()
    {
        QTemporaryDir dir;
        BaseHost host;
        QString error;
        QVERIFY(host.baseDocument()->createAt(dir.filePath(str("db.qbase")), str("donors"), &error));
        seedDonors(*host.base()->file());
        QVERIFY(host.base()->datasheet()->openTable(str("donors")));
        host.setSelectedKind(str("table"));
        host.setSelectedName(str("donors"));
        host.setCurrentView(str("datasheet"));
        ActionModel *actions = host.actions();
        ActionModelMenuSource source(host.menuTree(), actions, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY2(ids.size() > 70, qPrintable(QString::number(ids.size())));
        // Every QindaBase command is in the tree exactly once.
        for (ActionEntry *e : actions->entries()) {
            if (e->id().startsWith(str("base"))) {
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
            // file on disk resolve but are not pulled here.
            if (!entry->isEnabled() || id.startsWith(str("file")) || id.startsWith(str("help"))
                || id.startsWith(str("settings")) || id.startsWith(str("ai")) || id == str("baseCompactRepairAction")
                || id == str("baseBackupAction")) {
                continue;
            }
            const int before = triggered.count();
            QVERIFY2(actions->trigger(id), qPrintable(id));
            QCOMPARE(triggered.count(), before + 1);
            ++fired;
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join(QLatin1Char(' '))));
        QVERIFY(fired > 25);
    }
};

BASE_APP_TEST_MAIN(QindaBaseMenuExportTest)

#include "tst_qindabase_menu_export.moc"
