// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService, the QML API, over real databases: templates and the
// navigation list; table design and relationships planned, applied and
// undone through the document; renames that carry saved objects along;
// queries (select, parameters, action with a confirmed count); forms and
// reports (design files the toolkit opens); CSV import/export and XLSX;
// maintenance; plain SQLite files that need consent for system tables.

#include "base_app_test_support.h"

#include "base_access.h"

#include <qindatk/document_manager.h>
#include <qindatk/document_session.h>
#include <qindatk/local_data_design_document.h>
#include <workbook.h>
#include <xlsx_bridge.h>

#include <QFile>
#include <QUndoStack>

using namespace BaseAppTest;

namespace {

struct Fixture {
    QTemporaryDir dir;
    BaseDocument document;
    BaseService service{&document};
    QString error;
    explicit Fixture(bool seeded = true)
    {
        if (!document.createAt(dir.filePath(str("club.qbase")), str("donors"), &error)) {
            return;
        }
        if (seeded) {
            seedDonors(*service.file());
        }
    }
    QUrl url(const char *name) const { return QUrl::fromLocalFile(dir.filePath(str(name))); }
    QUndoStack *undo() const { return document.undoStack(); }
};

QVariantMap object(const QVariantList &list, const QString &kind, const QString &name)
{
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        if (m.value(str("kind")) == kind && m.value(str("name")) == name) {
            return m;
        }
    }
    return {};
}

} // namespace

class BaseServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void templatesAndNavigation()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        QCOMPARE(f.service.templates().size(), 6); // blank + five
        const QVariantList objects = f.service.objects();
        QCOMPARE(object(objects, str("table"), str("donors")).value(str("description")).toString(), str("40 record(s)"));
        QVERIFY(!object(objects, str("query"), str("Totals by Donor")).isEmpty());
        QVERIFY(!object(objects, str("form"), str("Donation Entry")).isEmpty());
        QVERIFY(!object(objects, str("report"), str("Donations by Donor")).isEmpty());
        QCOMPARE(f.service.databaseName(), str("club"));
        BaseDocument untitled;
        BaseService fresh(&untitled);
        QVERIFY(!fresh.hasDatabase());
        QVERIFY(ok(fresh.createUntitled(str("inventory"))));
        QVERIFY(untitled.isUntitled() && untitled.isDirty());
        QCOMPARE(fresh.databaseName(), str("New database"));
        QVERIFY(fresh.tableNames().contains(str("items")));
    }

    void tableDesignPlansAppliesAndUndoes()
    {
        Fixture f;
        QVariantMap design = f.service.tableDesign(str("donors"));
        QVERIFY(ok(design));
        QVariantList fields = design.value(str("fields")).toList();
        QVariantMap newsletter{{str("name"), str("newsletter")}, {str("type"), str("yesNo")}, {str("defaultValue"), str("Yes")}};
        fields.append(newsletter);
        design.insert(str("fields"), fields);
        const QVariantMap plan = f.service.planTableDesign(design);
        QVERIFY2(ok(plan), qPrintable(plan.value(str("error")).toString()));
        QVERIFY(plan.value(str("sql")).toString().startsWith(str("ALTER TABLE \"donors\" ADD COLUMN \"newsletter\" BOOLEAN DEFAULT 1;")));
        QVERIFY(plan.value(str("sql")).toString().contains(str("-- and"))); // bookkeeping summarised
        const int before = f.undo()->count();
        QVERIFY(ok(f.service.applyTableDesign(design)));
        QCOMPARE(f.undo()->count(), before + 1);
        QCOMPARE(f.service.file()->db().scalar(str("SELECT count(*) FROM donors WHERE newsletter=1")).toInt(), 40);
        f.undo()->undo();
        QVERIFY(!f.service.tableFields(str("donors")).last().toMap().value(str("name")).toString().contains(str("newsletter")));
        f.undo()->redo();
        QCOMPARE(f.service.tableFields(str("donors")).last().toMap().value(str("name")).toString(), str("newsletter"));
        // A new table, then a relationship to it.
        QVariantMap table = f.service.newTableDesign(str("pledges"));
        QVariantList pf = table.value(str("fields")).toList();
        pf.append(QVariantMap{{str("name"), str("donor_id")}, {str("type"), str("integer")}, {str("required"), true}});
        pf.append(QVariantMap{{str("name"), str("pledged")}, {str("type"), str("currency")}});
        table.insert(str("fields"), pf);
        QVERIFY(ok(f.service.applyTableDesign(table)));
        const QVariantMap rel{{str("childTable"), str("pledges")}, {str("childField"), str("donor_id")},
                              {str("parentTable"), str("donors")}, {str("parentField"), str("id")},
                              {str("onDelete"), str("cascade")}};
        QVERIFY(ok(f.service.planRelationship(rel, true)));
        QVERIFY(ok(f.service.applyRelationship(rel, true)));
        QCOMPARE(f.service.relationships().size(), 3);
        QVERIFY(ok(f.service.applyRelationship(rel, false)));
        QCOMPARE(f.service.relationships().size(), 2);
        // A bad design is refused with a reason and changes nothing.
        QVariantMap bad = f.service.tableDesign(str("donors"));
        QVariantList badFields = bad.value(str("fields")).toList();
        QVariantMap name = badFields[1].toMap();
        name.insert(str("name"), str("email")); // clashes with email
        badFields[1] = name;
        bad.insert(str("fields"), badFields);
        QVERIFY(f.service.planTableDesign(bad).value(str("error")).toString().contains(str("Two fields")));
    }

    void renamesCarrySavedObjects()
    {
        Fixture f;
        QVERIFY(ok(f.service.renameObject(str("table"), str("donors"), str("supporters"))));
        const QVariantMap form = f.service.objectInfo(str("form"), str("Donor"));
        QCOMPARE(form.value(str("definition")).toMap().value(str("source")).toMap().value(str("name")).toString(),
                 str("supporters"));
        QVERIFY(ok(f.service.compileQuery(f.service.queryDesign(str("Totals by Donor")))));
        QVERIFY(ok(f.service.renameObject(str("query"), str("Donation Details"), str("Gifts"))));
        const QVariantMap report = f.service.objectInfo(str("report"), str("Donations by Donor"));
        QCOMPARE(report.value(str("definition")).toMap().value(str("source")).toMap().value(str("name")).toString(),
                 str("Gifts"));
        const QVariantMap impact = f.service.deleteObjectImpact(str("table"), str("supporters"));
        QVERIFY(impact.value(str("blocked")).toBool()); // donations point at it
        QVERIFY(ok(f.service.deleteObject(str("query"), str("Gifts"))));
        f.undo()->undo();
        QVERIFY(!f.service.objectInfo(str("query"), str("Gifts")).isEmpty());
    }

    void queriesRunAndAsk()
    {
        Fixture f;
        QVariantMap r = f.service.runQuery(str("Totals by Donor"), {}, {});
        QVERIFY2(ok(r), qPrintable(r.value(str("error")).toString()));
        QVERIFY(!r.value(str("action")).toBool());
        QCOMPARE(f.service.datasheet()->sourceKind(), str("query"));
        QVERIFY(f.service.datasheet()->recordCount() > 20);
        QVERIFY(f.service.datasheet()->cellText(str("r1"), str("Total given")).startsWith(QLatin1Char('$')));
        r = f.service.runQuery(str("Donations in a Period"), {}, {});
        QVERIFY(r.value(str("needsParameters")).toBool());
        QCOMPARE(r.value(str("parameters")).toList().size(), 2);
        r = f.service.runQuery(str("Donations in a Period"), {},
                               {{str("p_start_date"), str("2026-01-01")}, {str("p_end_date"), str("2026-03-31")}});
        QVERIFY2(ok(r), qPrintable(r.value(str("error")).toString()));
        // An action query reports its count and runs only with it.
        QVariantMap update = f.service.newQueryDesign({str("donations")});
        update.insert(str("kind"), str("update"));
        update.insert(str("columns"), QVariantList{QVariantMap{{str("table"), str("donations")}, {str("field"), str("receipt_sent")},
                                                               {str("criteria"), str("No")}, {str("updateTo"), str("Yes")}}});
        r = f.service.runQuery(QString(), update, {});
        QVERIFY2(ok(r) && r.value(str("action")).toBool(), qPrintable(r.value(str("error")).toString()));
        const qint64 affected = r.value(str("affected")).toLongLong();
        QVERIFY(affected > 0);
        QVERIFY(!ok(f.service.runAction(update, {}, affected + 1)));
        QVERIFY(ok(f.service.runAction(update, {}, affected)));
        QCOMPARE(f.service.file()->db().scalar(str("SELECT count(*) FROM donations WHERE receipt_sent=0")).toInt(), 0);
        f.undo()->undo();
        QCOMPARE(f.service.file()->db().scalar(str("SELECT count(*) FROM donations WHERE receipt_sent=0")).toLongLong(),
                 affected);
        // Save a draft, then the SQL console.
        QVERIFY(ok(f.service.saveQuery(str("Unsent receipts"), update)));
        QVERIFY(f.service.queryNames().contains(str("Unsent receipts")));
        QVariantMap sql = f.service.previewSql(str("SELECT name FROM donors WHERE city = :city"), {});
        QVERIFY(sql.value(str("needsParameters")).toBool());
        sql = f.service.previewSql(str("SELECT name FROM donors WHERE city = :city"), {{str("city"), str("Portland")}});
        QVERIFY(ok(sql));
        QVERIFY(!f.service.previewSql(str("DELETE FROM donors"), {}).value(str("ok")).toBool());
    }

    void formsAndReportsOpenForTheToolkit()
    {
        Fixture f;
        const QVariantMap form = f.service.openForm(str("Donation Entry"));
        QVERIFY2(ok(form), qPrintable(form.value(str("error")).toString()));
        QindaTK::DocumentManager manager;
        QindaTK::DocumentSession *session = manager.openDocument(form.value(str("designPath")).toString());
        QVERIFY2(session != nullptr, qPrintable(manager.lastError()));
        auto *doc = qobject_cast<QindaTK::LocalDataDesignDocument *>(session->modelObject());
        QVERIFY(doc != nullptr);
        QCOMPARE(doc->tableName(), str("donations"));
        QVERIFY(doc->setFormTitle(str("Gift")));
        QVERIFY(ok(f.service.saveFormDesign(str("Donation Entry"), doc->design())));
        QCOMPARE(f.service.objectInfo(str("form"), str("Donation Entry")).value(str("definition")).toMap()
                     .value(str("title")).toString(),
                 str("Gift"));
        QVERIFY(ok(f.service.openForm(str("Donation Entry")))); // the saved design reopens
        // Records through the form: display names in, exact money.
        const QVariantList ids = f.service.formRowIds(str("donations"));
        QCOMPARE(ids.size(), 120);
        const QVariantMap first = f.service.formRecord(str("donations"), ids.first().toLongLong());
        // M80: a lookup is a combo box beside the designer: its stored key
        // comes apart from the designer's record, the choices name it.
        QVERIFY(!first.value(str("record")).toMap().contains(str("donor_id")));
        const QVariant donor = first.value(str("lookups")).toMap().value(str("donor_id"));
        QVERIFY(donor.toLongLong() > 0);
        const QVariantList choices = f.service.lookupChoices(str("donations"), str("donor_id"));
        QCOMPARE(choices.size(), 40);
        QVERIFY(std::any_of(choices.cbegin(), choices.cend(), [&](const QVariant &c) {
            return c.toMap().value(str("value")).toLongLong() == donor.toLongLong()
                   && c.toMap().value(str("text")).toString().contains(QLatin1Char(' '));
        }));
        const QVariantMap added = f.service.saveFormRecord(str("donations"), 0,
                                                          {{str("donor_id"), str("Ada Lovelace")}, {str("amount"), str("42.50")},
                                                           {str("given"), str("2026-03-30")}});
        QVERIFY2(ok(added), qPrintable(added.value(str("error")).toString()));
        QCOMPARE(f.service.file()->db().scalar(str("SELECT amount FROM donations WHERE id=?"), {added.value(str("rowid"))})
                     .toLongLong(),
                 qint64(4250));
        QVERIFY(!ok(f.service.saveFormRecord(str("donations"), 0, {{str("amount"), str("lots")}})));
        QVERIFY(ok(f.service.deleteFormRecord(str("donations"), added.value(str("rowid")).toLongLong())));
        // Reports: a snapshot and a design, grouped with totals.
        const QVariantMap report = f.service.openReport(str("Donations by Donor"), {});
        QVERIFY2(ok(report), qPrintable(report.value(str("error")).toString()));
        QCOMPARE(report.value(str("rowCount")).toInt(), 120);
        QVERIFY(QFile::exists(report.value(str("designPath")).toString()));
        QVERIFY(ok(f.service.openReport(str("Donations by Donor"), {}, true)));
        const QVariantMap wizard = f.service.createReport(
            str("Gifts by Method"),
            {{str("source"), QVariantMap{{str("kind"), str("table")}, {str("name"), str("donations")}}},
             {str("title"), str("Gifts by method")},
             {str("columns"), QVariantList{QVariantMap{{str("field"), str("given")}}, QVariantMap{{str("field"), str("amount")}}}},
             {str("groups"), QVariantList{QVariantMap{{str("field"), str("method")},
                                                      {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")},
                                                                                             {str("function"), str("sum")}}}}}}}});
        QVERIFY(ok(wizard));
        const QVariantMap gifts = f.service.openReport(str("Gifts by Method"), {});
        QVERIFY(ok(gifts));
        // Money prints as money: the currency column and its group sum carry
        // the file's symbol and decimals (QindaTK report number formats).
        QindaTK::DocumentSession *giftsSession = manager.openDocument(gifts.value(str("designPath")).toString());
        QVERIFY2(giftsSession != nullptr, qPrintable(manager.lastError()));
        QFile giftsFile(gifts.value(str("designPath")).toString());
        QVERIFY(giftsFile.open(QIODevice::ReadOnly));
        const QByteArray giftsDesign = giftsFile.readAll();
        QVERIFY2(giftsDesign.count("\"prefix\": \"$\"") + giftsDesign.count("\"prefix\":\"$\"") >= 2,
                 giftsDesign.constData());
        QVERIFY(f.service.reportParameters(str("Gifts by Method")).isEmpty());
        QVERIFY(ok(f.service.createForm(str("Quick Donor"), str("donors"), str("Donor"), {str("name"), str("email")})));
    }

    void importExportAndMaintenance()
    {
        Fixture f;
        const QString csv = f.dir.filePath(str("volunteers.csv"));
        QFile file(csv);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Name,Phone,Hours,Joined,Active\nAda,503-555-0101,12.5,2026-01-05,yes\nGrace,503-555-0102,3,2/1/2026,no\n");
        file.close();
        const QVariantMap proposal = f.service.csvProposal(QUrl::fromLocalFile(csv), QString(), QString(), true);
        QVERIFY(ok(proposal));
        QCOMPARE(proposal.value(str("plan")).toMap().value(str("table")).toString(), str("volunteers"));
        QCOMPARE(proposal.value(str("rowCount")).toInt(), 2);
        const int before = f.undo()->count();
        const QVariantMap imported = f.service.importCsv(QUrl::fromLocalFile(csv), proposal.value(str("plan")).toMap(),
                                                         QString(), true);
        QVERIFY2(ok(imported), qPrintable(imported.value(str("error")).toString()));
        QCOMPARE(imported.value(str("imported")).toInt(), 2);
        QCOMPARE(f.undo()->count(), before + 1); // table + rows: one step
        f.undo()->undo();
        QVERIFY(!f.service.tableNames().contains(str("volunteers")));
        f.undo()->redo();
        QVERIFY(f.service.tableNames().contains(str("volunteers")));
        // Export what the datasheet shows: CSV and XLSX.
        QVERIFY(f.service.datasheet()->openTable(str("donations")));
        QVERIFY(f.service.datasheet()->filterBySelection(str("r1"), str("method"), false));
        const int shown = f.service.datasheet()->recordCount();
        QVERIFY(ok(f.service.exportObject(str("table"), str("donations"), f.url("gifts.csv"), str("csv"))));
        QFile out(f.dir.filePath(str("gifts.csv")));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll().count('\n'), shown + 1);
        QVERIFY(ok(f.service.exportObject(str("table"), str("donors"), f.url("donors.xlsx"), str("xlsx"))));
        QindaOffice::Calc::Workbook book;
        QVERIFY(QindaOffice::Calc::IO::readXlsxFile(f.dir.filePath(str("donors.xlsx")), &book));
        QCOMPARE(book.sheetCount(), 1);
        // Maintenance.
        QVERIFY(ok(f.service.checkIntegrity()));
        QVERIFY(f.service.checkIntegrity().value(str("problems")).toStringList().isEmpty());
        QVERIFY(ok(f.service.backupNow()));
        const QVariantMap compact = f.service.compactAndRepair();
        QVERIFY(ok(compact));
        QCOMPARE(f.undo()->count(), 0); // history ends at a compact
        QVERIFY(f.service.accessTools().contains(str("available")));
        // Properties: the currency scale is fixed once money is stored.
        QVERIFY(!ok(f.service.setProperties({{str("currencySymbol"), str("€")}, {str("currencyDecimals"), 3}})));
        QVERIFY(ok(f.service.setProperties({{str("currencySymbol"), str("€")}, {str("currencyDecimals"), 2}})));
        QCOMPARE(f.service.properties().value(str("currencySymbol")).toString(), str("€"));
    }

    void plainSqliteNeedsConsent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(str("legacy.db"));
        {
            QindaOffice::Base::Connection c;
            QVERIFY(c.open(path, true, false, nullptr));
            QVERIFY(c.exec(str("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT); INSERT INTO people(name) VALUES('Ada')"))
                        .ok);
        }
        BaseDocument document;
        BaseService service(&document);
        QString error;
        QVERIFY2(document.load(path, &error), qPrintable(error));
        QVERIFY(!service.hasSystemTables());
        QVERIFY(service.datasheet()->openTable(str("people")));
        QVERIFY(ok(service.datasheet()->setCell(str("r1"), str("name"), str("Ada L."))));
        const QVariantMap saved = service.saveQuery(str("All"), service.newQueryDesign({str("people")}));
        QVERIFY(saved.value(str("needsSystemTables")).toBool());
        QVERIFY(ok(service.addSystemTables()));
        QVERIFY(service.hasSystemTables());
    }
};

BASE_APP_TEST_MAIN(BaseServiceTest)

#include "tst_qindabase_service.moc"
