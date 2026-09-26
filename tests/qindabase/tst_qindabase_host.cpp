// SPDX-License-Identifier: GPL-3.0-or-later
// BaseHost: every command id exists with an icon and a label; no shortcut
// is shared; commands follow the database and the view; Undo/Redo walk
// the document's steps (and the designer's history while one is open); a
// designer's unsaved design makes the window dirty and Save asks for it;
// Save As copies and switches; a report is the print source.

#include "base_app_test_support.h"

#include "standard_actions.h"

#include <qindatk/local_database_provider.h>
#include <qindatk/local_report_document_model.h>

#include <QFile>
#include <QSignalSpy>
#include <QUndoStack>

using namespace BaseAppTest;

namespace {

const QStringList kCommandIds{
    str("baseOpenObjectAction"), str("baseViewDesignAction"), str("baseViewSqlAction"), str("baseRenameObjectAction"),
    str("baseDeleteObjectAction"), str("baseNewRecordAction"), str("baseSaveRecordAction"), str("baseDeleteRecordAction"),
    str("baseRefreshAction"), str("baseSortAscendingAction"), str("baseSortDescendingAction"),
    str("baseFilterBySelectionAction"), str("baseFilterExcludingAction"), str("baseClearFiltersAction"),
    str("baseNewTableAction"), str("baseTableDesignAction"), str("baseQueryDesignAction"), str("baseNewFormAction"),
    str("baseFormWizardAction"), str("baseNewReportAction"), str("baseReportWizardAction"), str("baseImportCsvAction"),
    str("baseImportAccessAction"), str("baseExportCsvAction"), str("baseExportXlsxAction"), str("baseExportPdfAction"),
    str("baseRelationshipsAction"), str("baseSqlConsoleAction"), str("baseCompactRepairAction"),
    str("baseCheckIntegrityAction"), str("baseBackupAction"), str("baseDatabasePropertiesAction"),
    str("baseDesignAddFieldAction"), str("baseDesignDeleteFieldAction"), str("baseDesignMoveUpAction"),
    str("baseDesignMoveDownAction"), str("baseDesignPrimaryKeyAction"), str("baseDesignApplyAction"),
    str("baseDesignDiscardAction"), str("baseQueryRunAction"), str("baseQueryAddTableAction"), str("baseQueryClearAction"),
    str("baseFormPreviewAction"), str("baseReportRefreshAction"), str("baseTotalsRowAction"), str("baseFilterAction"),
    str("baseQueryTotalsAction"), str("baseShowNavigationAction"), str("baseQueryTypeSelectAction"),
    str("baseQueryTypeUpdateAction"), str("baseQueryTypeDeleteAction"), str("baseQueryTypeAppendAction")};

} // namespace

class BaseHostTest final : public QObject {
    Q_OBJECT
private slots:
    void commandsExistWithIconsAndUniqueShortcuts()
    {
        BaseHost host;
        QCOMPARE(kCommandIds.size(), 52);
        QSet<QString> shortcuts;
        for (QindaOffice::Quick::ActionEntry *entry : host.actions()->entries()) {
            const QString keys = entry->shortcutText();
            if (!keys.isEmpty()) {
                QVERIFY2(!shortcuts.contains(keys), qPrintable(entry->id() + str(" shares ") + keys));
                shortcuts.insert(keys);
            }
        }
        for (const QString &id : kCommandIds) {
            QindaOffice::Quick::ActionEntry *entry = host.action(id);
            QVERIFY2(entry != nullptr, qPrintable(id));
            QVERIFY2(!entry->iconName().isEmpty(), qPrintable(id));
            QVERIFY2(!entry->plainText().isEmpty(), qPrintable(id));
            QVERIFY2(!entry->isEnabled(), qPrintable(id + str(" enabled without a database")));
        }
        QCOMPARE(host.action(str("fileNewAction"))->plainText(), str("New Database…"));
    }

    void enablementFollowsTheView()
    {
        QTemporaryDir dir;
        BaseHost host;
        QString error;
        QVERIFY(host.baseDocument()->createAt(dir.filePath(str("x.qbase")), str("donors"), &error));
        seedDonors(*host.base()->file());
        QVERIFY(host.action(str("baseTableDesignAction"))->isEnabled());
        QVERIFY(!host.action(str("baseOpenObjectAction"))->isEnabled()); // nothing selected
        host.setSelectedKind(str("table"));
        host.setSelectedName(str("donors"));
        QVERIFY(host.action(str("baseOpenObjectAction"))->isEnabled());
        QVERIFY(!host.action(str("baseNewRecordAction"))->isEnabled()); // no datasheet yet
        QVERIFY(host.base()->datasheet()->openTable(str("donors")));
        host.setCurrentView(str("datasheet"));
        QVERIFY(host.action(str("baseNewRecordAction"))->isEnabled());
        QVERIFY(host.action(str("baseFilterBySelectionAction"))->isEnabled());
        QVERIFY(!host.action(str("baseDesignApplyAction"))->isEnabled());
        host.setCurrentView(str("tableDesign"));
        QCOMPARE(host.ribbonContexts(), QStringList{str("tableDesign")});
        QVERIFY(host.action(str("baseDesignApplyAction"))->isEnabled());
        QVERIFY(!host.action(str("baseNewRecordAction"))->isEnabled());
        host.setCurrentView(str("queryDesign"));
        QCOMPARE(host.ribbonContexts(), QStringList{str("queryDesign")});
        QVERIFY(host.action(str("baseQueryRunAction"))->isEnabled());
        host.setCurrentView(str("report"));
        QCOMPARE(host.ribbonContexts(), QStringList{str("formDesign")});
        QVERIFY(!host.action(str("filePrintAction"))->isEnabled()); // no report output yet
        // Every command the QML answers is announced by id.
        QSignalSpy requested(&host, &BaseHost::actionRequested);
        host.setCurrentView(str("datasheet"));
        QVERIFY(host.actions()->trigger(str("baseSortAscendingAction")));
        QCOMPARE(requested.last().first().toString(), str("baseSortAscendingAction"));
    }

    void undoWalksTheDocumentThenTheDesigner()
    {
        QTemporaryDir dir;
        BaseHost host;
        QString error;
        QVERIFY(host.baseDocument()->createAt(dir.filePath(str("x.qbase")), str("donors"), &error));
        seedDonors(*host.base()->file());
        DatasheetModel *sheet = host.base()->datasheet();
        QVERIFY(sheet->openTable(str("donors")));
        host.setCurrentView(str("datasheet"));
        QVERIFY(ok(sheet->setCell(str("r1"), str("name"), str("Ada B. Lovelace"))));
        QVERIFY(host.action(str("editUndoAction"))->isEnabled());
        QVERIFY(host.action(str("editUndoAction"))->plainText().startsWith(str("Undo Edit")));
        QVERIFY(host.actions()->trigger(str("editUndoAction")));
        QCOMPARE(sheet->cellText(str("r1"), str("name")), str("Ada Lovelace"));
        QVERIFY(host.actions()->trigger(str("editRedoAction")));
        QCOMPARE(sheet->cellText(str("r1"), str("name")), str("Ada B. Lovelace"));
        // A changed record behind the step: undo stops and says so.
        QVERIFY(host.base()->file()->db().run(str("UPDATE donors SET name='Someone' WHERE id=1")).ok);
        QSignalSpy message(&host, &BaseHost::messageRequested);
        QVERIFY(host.actions()->trigger(str("editUndoAction")));
        QTRY_COMPARE(message.count(), 1);
        QCOMPARE(host.baseDocument()->undoStack()->count(), 0);
        // With a designer open, Undo is the designer's.
        QSignalSpy requested(&host, &BaseHost::actionRequested);
        host.setCurrentView(str("tableDesign"));
        host.setDesignHistory(true);
        host.setDesignCanUndo(true);
        QVERIFY(host.actions()->trigger(str("editUndoAction")));
        QCOMPARE(requested.last().first().toString(), str("editUndoAction"));
    }

    void designDirtyAndSaveAs()
    {
        QTemporaryDir dir;
        BaseHost host;
        QString error;
        const QString path = dir.filePath(str("club.qbase"));
        QVERIFY(host.baseDocument()->createAt(path, QString(), &error));
        QVERIFY(!host.isDirty());
        QSignalSpy saveDesign(&host, &BaseHost::designSaveRequested);
        connect(&host, &BaseHost::designSaveRequested, &host, [&host] { host.setDesignDirty(false); });
        host.setDesignDirty(true);
        QVERIFY(host.isDirty());
        QString saveError;
        QVERIFY(host.baseDocument()->save(path, &saveError)); // Save flushes the design first
        QCOMPARE(saveDesign.count(), 1);
        QVERIFY(!host.isDirty());
        // Save As: a consistent copy, and the window works on it.
        QVERIFY(ok(host.base()->quickTable(str("people"))));
        const QString copy = dir.filePath(str("copy.qbase"));
        QVERIFY(host.baseDocument()->save(copy, &saveError));
        QCOMPARE(host.base()->workingPath(), copy);
        QVERIFY(host.base()->tableNames().contains(str("people")));
        QVERIFY(QFile::exists(path));
        QVERIFY(host.saveNameFilters().join(QLatin1Char(' ')).contains(str("*.qbase")));
    }

    void aReportIsThePrintSource()
    {
        QTemporaryDir dir;
        BaseHost host;
        QString error;
        QVERIFY(host.baseDocument()->createAt(dir.filePath(str("x.qbase")), str("donors"), &error));
        seedDonors(*host.base()->file());
        const QVariantMap opened = host.base()->openReport(str("Donations by Donor"), {});
        QVERIFY(ok(opened));
        QindaTK::LocalDatabaseProvider snapshot;
        QVERIFY(snapshot.open(opened.value(str("snapshotPath")).toString()));
        QindaTK::LocalReportDocumentModel output;
        const QVariantMap report{{str("tableName"), opened.value(str("table"))},
                                 {str("headerText"), str("Donations")},
                                 {str("groups"), QVariantList{QVariantMap{{str("field"), str("Donor")}}}}};
        QVERIFY2(output.build(&snapshot, report).value(str("ok")).toBool(), qPrintable(output.errorText()));
        host.setCurrentView(str("report"));
        host.setReportOutput(&output);
        QVERIFY(host.printable());
        QVERIFY(host.action(str("filePrintAction"))->isEnabled());
        QVERIFY(host.action(str("baseExportPdfAction"))->isEnabled());
        const QString pdf = dir.filePath(str("report.pdf"));
        QVERIFY(ok(host.exportReportPdf(QUrl::fromLocalFile(pdf))));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == QByteArray("%PDF-"));
        host.setCurrentView(str("datasheet")); // leaving the report clears the source
        QVERIFY(!host.printable());
        QVERIFY(!ok(host.exportReportPdf(QUrl::fromLocalFile(pdf))));
    }
};

BASE_APP_TEST_MAIN(BaseHostTest)

#include "tst_qindabase_host.moc"
