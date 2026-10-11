// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through BaseService over the seeded Donors & Donations
// starter: forms on QindaTK's positioned controls (D-311). A form saved
// before M89 is laid out once as it opens — its fields stacked in form
// order, lookups as combo or list boxes, M80's subform and button as a
// subform control (by form name) and a command button (a macro action id)
// — and saving the layout marks it positioned so nothing is laid out
// again; lookup labels and keys; calculated controls; Add Subform's
// choices and what a subform's form name shows.

#include "base_app_test_support.h"

#include "base_form_layout.h"
#include "base_objects.h"

#include <qindatk/local_data_design_store.h>

#include <QUndoStack>

using namespace BaseAppTest;

namespace {

struct Fixture {
    QTemporaryDir dir;
    BaseDocument document;
    BaseService service{&document};
    QString error;
    Fixture()
    {
        if (document.createAt(dir.filePath(str("club.qbase")), str("donors"), &error)) {
            seedDonors(*service.file());
        }
    }
    [[nodiscard]] QVariant scalar(const QString &sql, const QVariantList &bindings = {}) const
    {
        return service.file()->db().scalar(sql, bindings);
    }
    [[nodiscard]] QUndoStack *undo() const { return document.undoStack(); }
    // The design a form opens with: {design, controls (by id)}.
    QVariantMap open(const char *form)
    {
        const QVariantMap r = service.openForm(str(form));
        if (!ok(r)) {
            return {};
        }
        QindaTK::LocalDataDesignStore store;
        const QVariantMap loaded = store.loadFromFile(r.value(str("designPath")).toString());
        return loaded.value(str("design")).toMap();
    }
    [[nodiscard]] QVariantMap definition(const char *form) const
    {
        return QindaOffice::Base::ObjectStore::load(*service.file(), str("form"), str(form))->definition;
    }
};

QVariantList controlsOf(const QVariantMap &design)
{
    return design.value(str("form")).toMap().value(str("controls")).toList();
}

QVariantMap controlById(const QVariantMap &design, const char *id)
{
    for (const QVariant &v : controlsOf(design)) {
        if (v.toMap().value(str("id")).toString() == str(id)) {
            return v.toMap();
        }
    }
    return {};
}

} // namespace

class BaseM89Test final : public QObject {
    Q_OBJECT
private slots:
    void m86ValueListsKeepTheirTypedKeys()
    {
        namespace Engine = QindaOffice::Base;
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        Engine::ColumnInfo count;
        count.name = str("count");
        count.type = Engine::FieldType::Integer;
        count.lookup.values = {str("1"), str("2"), str("5")};
        QVERIFY(Engine::isLookupColumn(count));
        const auto items = Engine::lookupItems(*f.service.file(), count);
        QCOMPARE(items.labels, (QStringList{str("1"), str("2"), str("5")}));
        QCOMPARE(items.keys.size(), 3);
        QCOMPARE(items.keys.at(1).toLongLong(), qint64(2));
        QCOMPARE(Engine::lookupLabel(*f.service.file(), count, qint64(2)), str("2"));
        QCOMPARE(Engine::lookupLabel(*f.service.file(), count, qint64(7)), str("7"));
        count.type = Engine::FieldType::Text;
        count.lookup.values = {str("Check"), str("Card")};
        QCOMPARE(Engine::lookupItems(*f.service.file(), count).keys, (QVariantList{str("Check"), str("Card")}));
    }

    void anM80FormIsLaidOutAsItOpens()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        const QVariantMap donor = f.open("Donor");
        QVERIFY(!donor.isEmpty());
        // Eight fields, a label and a text box each, then the subform with
        // its caption and the button: 16 + 2 + 1.
        QCOMPARE(controlsOf(donor).size(), 19);
        const QVariantMap name = controlById(donor, "name");
        QCOMPARE(name.value(str("type")).toString(), str("textBox"));
        QCOMPARE(name.value(str("fieldName")).toString(), str("name"));
        QCOMPARE(controlById(donor, "name_label").value(str("caption")).toString(), str("Name"));
        QCOMPARE(controlById(donor, "donor_type_label").value(str("caption")).toString(), str("Donor type"));
        // Stacked in form order: each row under the one before.
        QVERIFY(controlById(donor, "donor_type").value(str("y")).toInt() > name.value(str("y")).toInt());
        // M80's subform: Donation Entry is the form over donations.
        const QVariantMap subform = controlById(donor, "subform1");
        QCOMPARE(subform.value(str("type")).toString(), str("subform"));
        QCOMPARE(subform.value(str("formName")).toString(), str("Donation Entry"));
        QCOMPARE(subform.value(str("linkMasterFields")).toString(), str("id"));
        QCOMPARE(subform.value(str("linkChildFields")).toString(), str("donor_id"));
        QVERIFY(subform.value(str("y")).toInt() > controlById(donor, "notes").value(str("y")).toInt());
        // M80's button ran the macro New Donation.
        const QVariantMap button = controlById(donor, "button1");
        QCOMPARE(button.value(str("type")).toString(), str("commandButton"));
        QCOMPARE(button.value(str("caption")).toString(), str("New donation"));
        QCOMPARE(button.value(str("actionId")).toString(), str("runMacro"));
        QCOMPARE(button.value(str("arguments")).toMap().value(str("macro")).toString(), str("New Donation"));
        // Lookups are combo boxes bound to their field; Yes/No a check box.
        const QVariantMap entry = f.open("Donation Entry");
        const QVariantMap donorBox = controlById(entry, "donor_id");
        QCOMPARE(donorBox.value(str("type")).toString(), str("comboBox"));
        QCOMPARE(donorBox.value(str("fieldName")).toString(), str("donor_id"));
        QCOMPARE(donorBox.value(str("rowSource")).toString(), str("donors"));
        QVERIFY(!donorBox.contains(str("items"))); // filled by the window, never stored
        QCOMPARE(controlById(entry, "receipt_sent").value(str("type")).toString(), str("checkBox"));
        QCOMPARE(controlById(entry, "receipt_sent").value(str("caption")).toString(), str("Receipt sent"));
        // Every bound field is in the design's binding (lookups included).
        const QStringList bound = entry.value(str("binding")).toMap().value(str("fields")).toStringList();
        QVERIFY(bound.contains(str("donor_id")));
        QVERIFY(bound.contains(str("campaign_id")));
    }

    void anM80ListBoxAndHiddenFieldKeepTheirChoice()
    {
        Fixture f;
        QVariantMap def = f.definition("Donation Entry");
        QVariantList fields = def.value(str("fields")).toList();
        for (QVariant &v : fields) {
            QVariantMap m = v.toMap();
            if (m.value(str("name")) == str("campaign_id")) {
                m.insert(str("control"), str("list"));
            }
            if (m.value(str("name")) == str("notes")) {
                m.insert(str("hidden"), true);
            }
            v = m;
        }
        def.insert(str("fields"), fields);
        QindaOffice::Base::Change change;
        QVERIFY(QindaOffice::Base::ObjectStore::save(*f.service.file(), str("form"), str("Donation Entry"), def, true, &change).ok);
        const QVariantMap entry = f.open("Donation Entry");
        QCOMPARE(controlById(entry, "campaign_id").value(str("type")).toString(), str("listBox"));
        QCOMPARE(controlById(entry, "campaign_id").value(str("height")).toInt(), QindaOffice::Base::FormLayoutUnits::listHeight);
        QVERIFY(controlById(entry, "notes").isEmpty());
    }

    void savingTheLayoutKeepsIt()
    {
        Fixture f;
        QVariantMap donor = f.open("Donor");
        // A user's edit: the button moves, a rectangle is added.
        QVariantMap form = donor.value(str("form")).toMap();
        QVariantList controls = form.value(str("controls")).toList();
        for (QVariant &v : controls) {
            QVariantMap m = v.toMap();
            if (m.value(str("id")) == str("button1")) {
                m.insert(str("x"), 200);
            }
            v = m;
        }
        controls.append(QVariantMap{{str("id"), str("box1")}, {str("type"), str("rectangle")}, {str("name"), str("Box1")},
                                    {str("x"), 0}, {str("y"), 0}, {str("width"), 400}, {str("height"), 600}});
        form.insert(str("controls"), controls);
        donor.insert(str("form"), form);
        const int steps = f.undo()->count();
        QVERIFY(ok(f.service.saveFormDesign(str("Donor"), donor)));
        QCOMPARE(f.undo()->count(), steps + 1);
        const QVariantMap def = f.definition("Donor");
        QVERIFY(def.value(str("positioned")).toBool());
        QVERIFY(!def.contains(str("controls"))); // M80's list is laid out, gone
        const QVariantMap again = f.open("Donor");
        QCOMPARE(controlsOf(again).size(), 20);
        QCOMPARE(controlById(again, "button1").value(str("x")).toInt(), 200);
        QCOMPARE(controlById(again, "box1").value(str("type")).toString(), str("rectangle"));
        // An emptied layout stays empty: nothing is laid out again.
        form.insert(str("controls"), QVariantList());
        donor.insert(str("form"), form);
        QVERIFY(ok(f.service.saveFormDesign(str("Donor"), donor)));
        QVERIFY(controlsOf(f.open("Donor")).isEmpty());
        // Undo brings the M80 form back, laid out again as it opens.
        f.undo()->undo();
        f.undo()->undo();
        QVERIFY(f.definition("Donor").contains(str("controls")));
        QCOMPARE(controlsOf(f.open("Donor")).size(), 19);
    }

    void lookupLabelsAndKeys()
    {
        Fixture f;
        const QVariantMap items = f.service.lookupItems(str("donations"), str("donor_id"));
        QVERIFY(ok(items));
        QVERIFY(items.value(str("lookup")).toBool());
        QVERIFY(items.value(str("required")).toBool());
        QCOMPARE(items.value(str("items")).toStringList().size(), 40);
        QCOMPARE(items.value(str("keys")).toList().size(), 40);
        QVERIFY(!f.service.lookupItems(str("donations"), str("amount")).value(str("lookup")).toBool());
        // A record shows the label; its storage value stays aside.
        const qint64 rowid = f.scalar(str("SELECT min(rowid) FROM donations")).toLongLong();
        const QVariant key = f.scalar(str("SELECT donor_id FROM donations WHERE rowid = ?"), {rowid});
        const QString name = f.scalar(str("SELECT name FROM donors WHERE id = ?"), {key}).toString();
        const QVariantMap record = f.service.formRecord(str("donations"), rowid);
        QCOMPARE(record.value(str("record")).toMap().value(str("donor_id")).toString(), name);
        QCOMPARE(record.value(str("stored")).toMap().value(str("donor_id")), key);
        // Two donors of one name: each label names its record by key.
        QVERIFY(f.service.file()->db().run(str("INSERT INTO donors(name) VALUES(?)"), {name}).ok);
        const qint64 twin = f.scalar(str("SELECT max(id) FROM donors")).toLongLong();
        const QStringList labels = f.service.lookupItems(str("donations"), str("donor_id")).value(str("items")).toStringList();
        QVERIFY(labels.contains(str("%1 (%2)").arg(name, key.toString())));
        QVERIFY(labels.contains(str("%1 (%2)").arg(name).arg(twin)));
        QCOMPARE(f.service.formRecord(str("donations"), rowid).value(str("record")).toMap().value(str("donor_id")).toString(),
                 str("%1 (%2)").arg(name, key.toString()));
        // Saving a key (what the window maps a label to) stores it.
        QVERIFY(ok(f.service.saveFormRecord(str("donations"), rowid, {{str("donor_id"), twin}})));
        QCOMPARE(f.scalar(str("SELECT donor_id FROM donations WHERE rowid = ?"), {rowid}).toLongLong(), twin);
    }

    void calculatedControls()
    {
        Fixture f;
        const QVariantMap doubled = f.service.evaluateFormExpression(str("donations"), str("=[amount] * 2"),
                                                                     {{str("amount"), str("$25.00")}});
        QVERIFY2(ok(doubled), qPrintable(doubled.value(str("error")).toString()));
        QCOMPARE(doubled.value(str("text")).toString(), str("$50.00"));
        const QVariantMap text = f.service.evaluateFormExpression(
            str("donations"), str("=[method] & \" gift\""), {{str("method"), str("Check")}});
        QCOMPARE(text.value(str("text")).toString(), str("Check gift"));
        // A blank value is Null, and so is what it touches.
        QCOMPARE(f.service.evaluateFormExpression(str("donations"), str("=[amount] + 1"), {}).value(str("text")).toString(),
                 QString());
        QVERIFY(!ok(f.service.evaluateFormExpression(str("donations"), str("=[nowhere] * 2"), {})));
        QVERIFY(!ok(f.service.evaluateFormExpression(str("donations"), str("=Sum([amount])"), {})));
        QVERIFY(ok(f.service.evaluateFormExpression(str("donations"), QString(), {})));
    }

    void subformChoicesAndTheTableTheyShow()
    {
        Fixture f;
        const QVariantList choices = f.service.subformChoices(str("Donor"));
        QCOMPARE(choices.size(), 1);
        const QVariantMap gifts = choices.first().toMap();
        QCOMPARE(gifts.value(str("table")).toString(), str("donations"));
        QCOMPARE(gifts.value(str("formName")).toString(), str("Donation Entry"));
        QCOMPARE(gifts.value(str("linkChild")).toString(), str("donor_id"));
        QCOMPARE(gifts.value(str("linkParent")).toString(), str("id"));
        QCOMPARE(f.service.subformTable(str("Donation Entry")).value(str("table")).toString(), str("donations"));
        QCOMPARE(f.service.subformTable(str("donations")).value(str("table")).toString(), str("donations"));
        QVERIFY(!ok(f.service.subformTable(str("Nowhere"))));
        QVERIFY(!ok(f.service.subformTable(QString())));
    }
};

BASE_APP_TEST_MAIN(BaseM89Test)
#include "tst_qindabase_m89.moc"
