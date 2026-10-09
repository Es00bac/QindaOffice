// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaBooks host must pass the desktop's own
// validator, and EVERY exported command must resolve in the host's
// ActionModel and fire (the same gate as the other apps' menu-export tests).

#include "books_app_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QSignalSpy>
#include <QUuid>

using namespace BooksAppTest;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModel;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class QindaBooksMenuExportTest final : public QObject {
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
        for (const bool company : {false, true}) {
            QTemporaryDir dir;
            BooksHost host;
            if (company) {
                QVERIFY(ok(host.books()->createCompany(url(dir, QStringLiteral("co.qbooks")),
                                                       {{QStringLiteral("name"), QStringLiteral("Menu Co")}},
                                                       QStringLiteral("general"))));
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
            QCOMPARE(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                                          QStringLiteral("Sales"), QStringLiteral("Expenses"), QStringLiteral("Time"),
                                          QStringLiteral("Banking"),
                                          QStringLiteral("Accounting"), QStringLiteral("Reports"), QStringLiteral("AI"),
                                          QStringLiteral("Settings"), QStringLiteral("Help")}));
        }
    }

    void everyExportedCommandResolvesAndFires()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(host.books()->createCompany(url(dir, QStringLiteral("co.qbooks")),
                                               {{QStringLiteral("name"), QStringLiteral("Menu Co")}}, QStringLiteral("general"))));
        host.setSelectedDocumentId(1);
        host.setPrintSource(QStringLiteral("report"), QStringLiteral("balance_sheet"));
        ActionModel *actions = host.actions();
        ActionModelMenuSource source(host.menuTree(), actions, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY2(ids.size() > 50, qPrintable(QString::number(ids.size())));
        // Every books command is in the tree exactly once.
        for (ActionEntry *e : actions->entries()) {
            if (e->id().startsWith(QStringLiteral("books"))) {
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
            // Commands that close, quit, open system dialogs or launch
            // another program resolve but are not pulled here.
            if (!entry->isEnabled() || id.startsWith(QStringLiteral("file")) || id.startsWith(QStringLiteral("help"))
                || id.startsWith(QStringLiteral("settings")) || id.startsWith(QStringLiteral("ai"))
                || id == QStringLiteral("booksEmailDocumentAction")) {
                continue;
            }
            const int before = triggered.count();
            QVERIFY2(actions->trigger(id), qPrintable(id));
            QCOMPARE(triggered.count(), before + 1);
            ++fired;
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join(QLatin1Char(' '))));
        QVERIFY(fired > 30);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksMenuExportTest)

#include "tst_qindabooks_menu_export.moc"
