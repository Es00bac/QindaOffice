// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaNote host must pass the desktop's own
// validator, and EVERY exported command must resolve and fire in the
// host's ActionModel — the defect the Widgets suite shipped (plan V5: 71
// of 72 panel commands dead) is a hard failure here, not an expected one.

#include "note_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QSignalSpy>
#include <QUuid>

using namespace QindaOffice::Apps::Notes;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModel;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class NoteMenuExportTest final : public QObject {
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
        NoteHost host;
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY2(snapshot.complete, qPrintable(QStringLiteral("incomplete: ") + snapshot.defectCode));
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY2(result.accepted,
                 qPrintable(QStringLiteral("rejected: %1 at %2").arg(result.reasonCode, result.path)));
        // File Edit View Ink Page AI Settings Help (mnemonics travel as
        // mnemonicIndex, not as '&' in the text).
        QStringList titles;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            titles.append(item.text);
        }
        QCOMPARE(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                                      QStringLiteral("Ink"), QStringLiteral("Page"), QStringLiteral("AI"),
                                      QStringLiteral("Settings"), QStringLiteral("Help")}));
    }

    void everyExportedCommandResolvesAndFires()
    {
        NoteHost host;
        ActionModel *actions = host.actions();
        ActionModelMenuSource source(host.menuTree(), actions, QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY2(ids.size() > 60, qPrintable(QString::number(ids.size())));
        QSignalSpy triggered(actions, &ActionModel::triggered);
        QStringList unresolved;
        int fired = 0;
        for (const QString &id : ids) {
            ActionEntry *entry = actions->entry(id);
            if (entry == nullptr) {
                unresolved.append(id);
                continue;
            }
            // Commands that open a dialog or quit are resolved but not
            // pulled here; the enabled, side-effect-free ones fire.
            if (!entry->isEnabled() || id.startsWith(QStringLiteral("file"))
                || id.endsWith(QStringLiteral("Delete")) || id.contains(QStringLiteral("Rename"))
                || id == QStringLiteral("noteSectionNew") || id.startsWith(QStringLiteral("help"))
                || id.startsWith(QStringLiteral("settings")) || id.startsWith(QStringLiteral("ai"))
                || id == QStringLiteral("noteAddImageAction")
                || id == QStringLiteral("notePenColorAction")
                || id == QStringLiteral("noteExportPdf") || id.startsWith(QStringLiteral("editFind"))
                || id.startsWith(QStringLiteral("editReplace"))) {
                continue;
            }
            const int before = triggered.count();
            QVERIFY2(actions->trigger(id), qPrintable(id));
            QCOMPARE(triggered.count(), before + 1);
            ++fired;
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join(QLatin1Char(' '))));
        QVERIFY(fired > 20);
    }
};

NOTE_TEST_MAIN(NoteMenuExportTest)

#include "tst_note_menu_export.moc"
