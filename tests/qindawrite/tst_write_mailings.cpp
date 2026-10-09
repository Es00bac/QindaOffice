// SPDX-License-Identifier: GPL-3.0-or-later
// Word's Mailings in QindaWrite through the real window (M77, D-086):
// letters merged from a CSV and an Excel workbook (fields, Address Block,
// Greeting Line, SKIPIF), Edit Recipient List, Preview Results, Finish &
// Merge (documents, a range, PDF), e-mail handed to a fake QindaMail,
// QindaMail's People and contact groups, Check for Errors, w:mailMerge
// saved and reopened, envelopes and labels (Update Labels; one page).
// QQO_GRAB_DIR keeps grabs of the tab, the dialogs, a sheet, a preview.

#include "write_test_support.h"

#include "docx_merge.h"
#include "docx_properties.h"
#include "qindawrite/src/write_envelopes.h"
#include "qindawrite/src/write_mailmerge.h"
#include "support/quick_dump.h"
#include "workbook.h"
#include "xlsx_bridge.h"

#include "ooxml_package.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextTable>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace Docx = QindaOffice::Docx;
namespace Calc = QindaOffice::Calc;

namespace {

const char *kCsv = "First Name,Last Name,Company,Address Line 1,City,State,ZIP Code,Country or Region,Email\r\n"
                   "Ada,Lovelace,Analytical Engines,12 St James's Square,London,,SW1Y 4JH,United Kingdom,ada@example.org\r\n"
                   "Grace,Hopper,,1 Navy Way,Arlington,VA,22202,United States,grace@example.org\r\n"
                   "Katherine,Johnson,NASA,Langley Bldg 1244,Hampton,VA,23681,United States,\r\n";

QString writeCsv(QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("people.csv"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(kCsv);
    return path;
}

QString writeWorkbook(QTemporaryDir &dir, bool twoSheets)
{
    Calc::Workbook book;
    if (twoSheets) {
        book.addSheet(QStringLiteral("Notes"));
        book.setCellValue(0, {0, 0}, QStringLiteral("Not a list"));
    }
    const qint32 sheet = book.sheetCount();
    book.addSheet(QStringLiteral("Clients"));
    const QStringList header{QStringLiteral("First Name"), QStringLiteral("Last Name"), QStringLiteral("City"),
                             QStringLiteral("Balance")};
    for (int c = 0; c < header.size(); ++c) {
        book.setCellValue(sheet, {0, c}, header.at(c));
    }
    const QList<QStringList> rows{{QStringLiteral("Edsger"), QStringLiteral("Dijkstra"), QStringLiteral("Austin")},
                                  {QStringLiteral("Barbara"), QStringLiteral("Liskov"), QStringLiteral("Boston")}};
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < 3; ++c) {
            book.setCellValue(sheet, {r + 1, c}, rows.at(r).at(c));
        }
        book.setCellValue(sheet, {r + 1, 3}, double(100 * (r + 1)));
    }
    const QString path = dir.filePath(QStringLiteral("clients.xlsx"));
    QString error;
    if (!Calc::IO::writeXlsxFile(book, path, &error)) {
        qWarning() << error;
    }
    return path;
}

MailMergeController *merge(Scene &scene)
{
    return scene.host.mailMergeController();
}

void typeLetter(Scene &scene)
{
    QTextCursor c(scene.doc->textDocument());
    scene.control()->setTextCursor(c);
    merge(scene)->insertAddressBlock({});
    c = scene.control()->textCursor();
    c.insertBlock();
    scene.control()->setTextCursor(c);
    merge(scene)->insertGreetingLine({});
    c = scene.control()->textCursor();
    c.insertBlock();
    c.insertText(QStringLiteral("Your town: "));
    scene.control()->setTextCursor(c);
    merge(scene)->insertField(QStringLiteral("City"));
}

// The merged document openDocumentWindow() handed on, read back.
std::unique_ptr<WriteDocument> made(const QList<QVariant> &signal)
{
    auto doc = std::make_unique<WriteDocument>();
    if (!doc->loadDocxBytes(signal.value(0).toByteArray())) {
        return nullptr;
    }
    return doc;
}

int sections(const QTextDocument *doc)
{
    int count = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        count += b.blockFormat().hasProperty(Docx::kSectionXml) ? 1 : 0;
    }
    return count;
}

} // namespace

class WriteMailingsTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void mergeFromCsvPreviewAndFinish();
    void mergeFromWorkbook();
    void skipIfDropsARecord();
    void recipientListIncludesFiltersSorts();
    void emailMessagesHandedToQindaMail();
    void peopleFromQindaMail();
    void checkForErrorsAndSavedMerge();
    void envelopesAndLabels();
    void grabs();
};

void WriteMailingsTest::mergeFromCsvPreviewAndFinish()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QVERIFY(!scene.host.action(QStringLiteral("writeMergeAddressBlockAction"))->isEnabled());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeLettersAction")));
    QCOMPARE(mm->kind(), QStringLiteral("letters"));
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
    QCOMPARE(mm->recipientCount(), 3);
    QVERIFY(scene.host.action(QStringLiteral("writeMergeAddressBlockAction"))->isEnabled());
    QCOMPARE(scene.host.action(QStringLiteral("writeMergeField1"))->text(), QStringLiteral("Last Name"));
    QVERIFY(scene.host.action(QStringLiteral("writeMergeField0"))->isVisible());
    QVERIFY(!scene.host.action(QStringLiteral("writeMergeField12"))->isVisible());
    typeLetter(scene);
    QCOMPARE(Docx::mergeFieldSpans(*scene.doc->textDocument()).size(), 3);
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("«AddressBlock»")));
    // One undo step per field.
    scene.control()->undo();
    QCOMPARE(Docx::mergeFieldSpans(*scene.doc->textDocument()).size(), 2);
    scene.control()->redo();
    QCOMPARE(Docx::mergeFieldSpans(*scene.doc->textDocument()).size(), 3);
    // Preview Results: the page shows the first record, then the next.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergePreviewAction")));
    QVERIFY(mm->previewing());
    QTextDocument *preview = scene.page->previewDocument();
    QVERIFY(preview != nullptr);
    QVERIFY2(preview->toPlainText().startsWith(QStringLiteral("Ada Lovelace")), qPrintable(preview->toPlainText()));
    QVERIFY(preview->toPlainText().contains(QStringLiteral("Your town: London")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeNextAction")));
    QCOMPARE(mm->previewRecord(), 2);
    QVERIFY(scene.page->previewDocument()->toPlainText().contains(QStringLiteral("Dear Hopper,")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeLastAction")));
    QCOMPARE(mm->previewRecord(), 3);
    QCOMPARE(mm->findRecipient(QStringLiteral("hopper"), QString(), 2), 1);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeFirstAction")));
    QCOMPARE(mm->previewRecord(), 1);
    // The body under the preview still has its fields.
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("«City»")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergePreviewAction")));
    QVERIFY(scene.page->previewDocument() == nullptr);
    // Finish & Merge ▸ Edit Individual Documents: a new window's document.
    QSignalSpy madeSpy(&scene.host, &WriteHost::documentMade);
    QVERIFY(mm->finishToDocument({{QStringLiteral("records"), QStringLiteral("all")}}));
    QCOMPARE(madeSpy.size(), 1);
    auto merged = made(madeSpy.first());
    QVERIFY(merged);
    const QString text = merged->textDocument()->toPlainText();
    QCOMPARE(sections(merged->textDocument()), 2); // a section per record
    QCOMPARE(text.count(QStringLiteral("Your town: ")), 3);
    QVERIFY2(text.contains(QStringLiteral("Ada Lovelace\nAnalytical Engines")), qPrintable(text)); // Address Block lines
    QVERIFY(text.contains(QStringLiteral("Your town: Hampton")));
    QVERIFY(!text.contains(QStringLiteral("«")));
    QVERIFY(merged->isDirty()); // a new, unsaved document
    // A range: records 2 to 3.
    QVERIFY(mm->finishToDocument({{QStringLiteral("records"), QStringLiteral("range")}, {QStringLiteral("from"), 2},
                                  {QStringLiteral("to"), 3}}));
    auto range = made(madeSpy.last());
    QCOMPARE(range->textDocument()->toPlainText().count(QStringLiteral("Your town: ")), 2);
    QVERIFY(!range->textDocument()->toPlainText().contains(QStringLiteral("London")));
    // Merge to PDF.
    const QString pdf = dir.filePath(QStringLiteral("letters.pdf"));
    QVERIFY(mm->finishToPdf(QUrl::fromLocalFile(pdf), {{QStringLiteral("records"), QStringLiteral("all")}}));
    QVERIFY(QFileInfo(pdf).size() > 1000);
}

void WriteMailingsTest::mergeFromWorkbook()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeWorkbook(dir, false))));
    QCOMPARE(mm->fields(), QStringList({QStringLiteral("First Name"), QStringLiteral("Last Name"), QStringLiteral("City"),
                                        QStringLiteral("Balance")}));
    QCOMPARE(mm->recipientCount(), 2);
    QCOMPARE(scene.doc->mailMerge().table, QStringLiteral("Clients"));
    QTextCursor c(scene.doc->textDocument());
    c.insertText(QStringLiteral("Hello "));
    scene.control()->setTextCursor(c);
    mm->insertField(QStringLiteral("First Name"));
    c = scene.control()->textCursor();
    c.insertText(QStringLiteral(", balance "));
    scene.control()->setTextCursor(c);
    mm->insertField(QStringLiteral("Balance"));
    QSignalSpy madeSpy(&scene.host, &WriteHost::documentMade);
    QVERIFY(mm->finishToDocument({}));
    auto merged = made(madeSpy.first());
    QVERIFY(merged);
    const QString text = merged->textDocument()->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Hello Edsger, balance 100")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("Hello Barbara, balance 200")));
    // A workbook with two sheets asks which one (Word's Select Table).
    QSignalSpy sheetSpy(mm, &MailMergeController::sheetRequested);
    const QString two = writeWorkbook(dir, true);
    QVERIFY(mm->useList(QUrl::fromLocalFile(two)));
    QCOMPARE(sheetSpy.size(), 1);
    QCOMPARE(sheetSpy.first().at(1).toStringList(), QStringList({QStringLiteral("Notes"), QStringLiteral("Clients")}));
    QVERIFY(mm->useWorkbookSheet(QUrl::fromLocalFile(two), QStringLiteral("Clients")));
    QCOMPARE(mm->recipientCount(), 2);
}

void WriteMailingsTest::skipIfDropsARecord()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
    QTextCursor c(scene.doc->textDocument());
    c.insertText(QStringLiteral("To "));
    scene.control()->setTextCursor(c);
    mm->insertField(QStringLiteral("First Name"));
    c = scene.control()->textCursor();
    c.insertText(QStringLiteral(" "));
    scene.control()->setTextCursor(c);
    mm->insertRule({{QStringLiteral("rule"), QStringLiteral("skipif")}, {QStringLiteral("field"), QStringLiteral("City")},
                    {QStringLiteral("op"), QStringLiteral("=")}, {QStringLiteral("value"), QStringLiteral("London")}});
    mm->insertRule({{QStringLiteral("rule"), QStringLiteral("if")}, {QStringLiteral("field"), QStringLiteral("State")},
                    {QStringLiteral("op"), QStringLiteral("=")}, {QStringLiteral("value"), QStringLiteral("VA")},
                    {QStringLiteral("then"), QStringLiteral("in Virginia")}, {QStringLiteral("otherwise"), QStringLiteral("abroad")}});
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("«Skip Record If…»")));
    QSignalSpy madeSpy(&scene.host, &WriteHost::documentMade);
    QVERIFY(mm->finishToDocument({}));
    auto merged = made(madeSpy.first());
    const QString text = merged->textDocument()->toPlainText();
    QCOMPARE(text.count(QStringLiteral("To ")), 2); // London's Ada is skipped
    QVERIFY(text.contains(QStringLiteral("To Grace in Virginia")));
    QVERIFY(!text.contains(QStringLiteral("Ada")));
    QCOMPARE(sections(merged->textDocument()), 1);
}

void WriteMailingsTest::recipientListIncludesFiltersSorts()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
    mm->setIncluded(0, false);
    QCOMPARE(mm->recipientCount(), 2);
    QCOMPARE(mm->recipientRows().size(), 3);
    QVERIFY(!mm->recipientRows().first().toMap().value(QStringLiteral("included")).toBool());
    mm->setFilterSort({{QStringLiteral("filters"),
                        QVariantList{QVariantMap{{QStringLiteral("field"), QStringLiteral("State")},
                                                 {QStringLiteral("op"), QStringLiteral("equal")},
                                                 {QStringLiteral("value"), QStringLiteral("VA")}}}},
                       {QStringLiteral("sorts"),
                        QVariantList{QVariantMap{{QStringLiteral("field"), QStringLiteral("Last Name")},
                                                 {QStringLiteral("descending"), true}}}}});
    QCOMPARE(mm->order(), QList<int>({2, 1})); // Johnson, Hopper
    mm->setAllIncluded(true);
    QCOMPARE(mm->recipientCount(), 2); // the filter still holds
    mm->setFilterSort({});
    QCOMPARE(mm->recipientCount(), 3);
    mm->sortRecipients(QStringLiteral("City"), false);
    QCOMPARE(mm->order(), QList<int>({1, 2, 0})); // Arlington, Hampton, London
    QCOMPARE(mm->duplicates().size(), 0);
}

void WriteMailingsTest::emailMessagesHandedToQindaMail()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QList<QStringList> handed;
    mm->launchMail = [&handed](const QStringList &arguments) {
        handed.append(arguments);
        return true;
    };
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeEmailAction")));
    QCOMPARE(mm->kind(), QStringLiteral("email"));
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
    QTextCursor c(scene.doc->textDocument());
    c.insertText(QStringLiteral("Hi "));
    scene.control()->setTextCursor(c);
    mm->insertField(QStringLiteral("First Name"));
    QCOMPARE(mm->emailInfo().value(QStringLiteral("to")).toString(), QStringLiteral("Email"));
    const int sent = mm->sendEmails({{QStringLiteral("to"), QStringLiteral("Email")},
                                     {QStringLiteral("subject"), QStringLiteral("Hello & welcome")},
                                     {QStringLiteral("format"), QStringLiteral("text")}});
    QCOMPARE(sent, 2); // Katherine has no address
    QCOMPARE(handed.size(), 2);
    QCOMPARE(handed.at(0).size(), 1);
    QVERIFY2(handed.at(0).at(0).startsWith(QStringLiteral("mailto:ada@example.org?subject=Hello%20%26%20welcome&body=Hi%20Ada")),
             qPrintable(handed.at(0).at(0)));
    QVERIFY(handed.at(1).at(0).contains(QStringLiteral("Hi%20Grace")));
    QCOMPARE(scene.doc->mailMerge().addressField, QStringLiteral("Email"));
    // As an attachment: the merged document beside the mailto: URL.
    handed.clear();
    QCOMPARE(mm->sendEmails({{QStringLiteral("to"), QStringLiteral("Email")}, {QStringLiteral("subject"), QStringLiteral("Doc")},
                             {QStringLiteral("format"), QStringLiteral("attachment")}, {QStringLiteral("records"), QStringLiteral("current")}}),
             1);
    QCOMPARE(handed.first().at(0), QStringLiteral("--attach"));
    QVERIFY(handed.first().at(1).endsWith(QStringLiteral(".docx")));
    QVERIFY(QFileInfo(handed.first().at(1)).size() > 1000);
    QCOMPARE(handed.first().at(2), QStringLiteral("mailto:ada@example.org?subject=Doc"));
}

void WriteMailingsTest::peopleFromQindaMail()
{
    // QindaMail's address book as its store writes it (contact.h toJson).
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/qindaoffice/mail/contacts.json");
    QDir().mkpath(QFileInfo(path).path());
    const QJsonArray contacts{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("givenName"), QStringLiteral("Linus")},
                    {QStringLiteral("familyName"), QStringLiteral("Torvalds")},
                    {QStringLiteral("emails"), QJsonArray{QJsonObject{{QStringLiteral("value"), QStringLiteral("linus@example.org")}}}},
                    {QStringLiteral("addresses"),
                     QJsonArray{QJsonObject{{QStringLiteral("street"), QStringLiteral("1 Kernel Way\nUnit 2")},
                                            {QStringLiteral("locality"), QStringLiteral("Portland")},
                                            {QStringLiteral("region"), QStringLiteral("OR")}}}},
                    {QStringLiteral("categories"), QJsonArray{QStringLiteral("Clients")}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("2")}, {QStringLiteral("uid"), QStringLiteral("u-2")},
                    {QStringLiteral("givenName"), QStringLiteral("Margaret")}, {QStringLiteral("familyName"), QStringLiteral("Hamilton")}},
        // A contact group (M76): a card of the book and a raw address.
        QJsonObject{{QStringLiteral("id"), QStringLiteral("3")}, {QStringLiteral("kind"), QStringLiteral("group")},
                    {QStringLiteral("displayName"), QStringLiteral("Board")},
                    {QStringLiteral("members"), QJsonArray{QJsonObject{{QStringLiteral("uri"), QStringLiteral("urn:uuid:u-2")}},
                                                           QJsonObject{{QStringLiteral("uri"), QStringLiteral("mailto:grace@example.org")},
                                                                       {QStringLiteral("name"), QStringLiteral("Grace Hopper")}}}}}};
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("schema"), QStringLiteral("qindaoffice.mail.contacts/1")},
                                         {QStringLiteral("contacts"), contacts}})
                   .toJson());
    file.close();
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    auto *mm = merge(scene);
    const QVariantList groups = mm->peopleGroups(); // everyone (not the group), Board, Clients
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).toMap().value(QStringLiteral("count")).toInt(), 2);
    QCOMPARE(groups.at(1).toMap().value(QStringLiteral("group")).toString(), QStringLiteral("Board"));
    QVERIFY(mm->usePeople(QStringLiteral("Board")));
    QCOMPARE(mm->recipientCount(), 2);
    QCOMPARE(mm->data().value(0, QStringLiteral("Last Name")), QStringLiteral("Hamilton"));
    QCOMPARE(mm->data().value(1, QStringLiteral("E-mail Address")), QStringLiteral("grace@example.org"));
    QVERIFY(mm->usePeople(QStringLiteral("Clients")));
    QCOMPARE(mm->recipientCount(), 1);
    QCOMPARE(mm->data().value(0, QStringLiteral("Address Line 2")), QStringLiteral("Unit 2"));
    QCOMPARE(scene.doc->mailMerge().dataSource, QStringLiteral("qindamail:people/Clients"));
    QVERIFY(mm->addressBlockPreview({}).startsWith(QStringLiteral("Linus Torvalds\n1 Kernel Way\nUnit 2\nPortland, OR")));
    QFile::remove(path);
}

void WriteMailingsTest::checkForErrorsAndSavedMerge()
{
    QTemporaryDir dir;
    const QString saved = dir.filePath(QStringLiteral("main.docx"));
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto *mm = merge(scene);
        QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
        QVERIFY(mm->checkErrors().contains(QStringLiteral("The document has no merge fields: every copy would be the same.")));
        QTextCursor c(scene.doc->textDocument());
        scene.control()->setTextCursor(c);
        mm->insertField(QStringLiteral("Region"));
        mm->insertField(QStringLiteral("City"));
        const QStringList errors = mm->checkErrors();
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains(QStringLiteral("Region")));
        QString error;
        QVERIFY2(scene.doc->save(saved, &error), qPrintable(error));
        const auto package = QindaOffice::Ooxml::Package::load(saved);
        const QByteArray settings = package->data(QStringLiteral("word/settings.xml"));
        QVERIFY2(settings.contains("<w:mainDocumentType w:val=\"formLetters\"/><w:dataType w:val=\"textFile\"/>"),
                 settings.constData());
    }
    // Reopened: a merge document again, its recipients read from the CSV.
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(scene.host.loadPath(saved));
    QCOMPARE(merge(scene)->kind(), QStringLiteral("letters"));
    QCOMPARE(merge(scene)->recipientCount(), 3);
    QVERIFY(scene.host.action(QStringLiteral("writeMergeLettersAction"))->isChecked());
}

void WriteMailingsTest::envelopesAndLabels()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    EnvelopesController *envelopes = scene.host.envelopesController();
    const QSizeF paper = scene.doc->pageSize();
    QVERIFY(envelopes->applyEnvelope({{QStringLiteral("mode"), QStringLiteral("add")},
                                      {QStringLiteral("delivery"), QStringLiteral("Grace Hopper\n1 Navy Way")},
                                      {QStringLiteral("returnAddress"), QStringLiteral("Ada Lovelace")},
                                      {QStringLiteral("size"), QStringLiteral("DL")}}));
    QCOMPARE(qRound(scene.doc->pageSize().width()), 831); // the empty document became the envelope
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Grace Hopper")));
    scene.control()->undo(); // one step: the text and the paper
    QCOMPARE(scene.doc->pageSize(), paper);
    QVERIFY(scene.doc->textDocument()->isEmpty());
    // Labels ▸ New Document: a sheet of the same label in a new window.
    QSignalSpy madeSpy(&scene.host, &WriteHost::documentMade);
    QVERIFY(envelopes->applyLabels({{QStringLiteral("mode"), QStringLiteral("new")}, {QStringLiteral("product"), QStringLiteral("Avery US Letter|5160")},
                                    {QStringLiteral("address"), QStringLiteral("Grace Hopper\n1 Navy Way")},
                                    {QStringLiteral("fullPage"), true}}));
    auto sheet = made(madeSpy.first());
    QVERIFY(sheet);
    auto *table = qobject_cast<QTextTable *>(sheet->textDocument()->rootFrame()->childFrames().value(0));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 10);
    QCOMPARE(sheet->textDocument()->toPlainText().count(QStringLiteral("Grace Hopper")), 30);
    // A single label at row 2, column 2.
    QVERIFY(envelopes->applyLabels({{QStringLiteral("mode"), QStringLiteral("new")}, {QStringLiteral("product"), QStringLiteral("Avery A4/A5|L7163")},
                                    {QStringLiteral("address"), QStringLiteral("One")}, {QStringLiteral("fullPage"), false},
                                    {QStringLiteral("row"), 2}, {QStringLiteral("column"), 2}}));
    auto one = made(madeSpy.last());
    QCOMPARE(one->textDocument()->toPlainText().count(QStringLiteral("One")), 1);
    QCOMPARE(qRound(one->pageSize().width()), 794); // A4
    // Start Mail Merge ▸ Labels: this document becomes the sheet.
    QTemporaryDir dir;
    auto *mm = merge(scene);
    QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergeLabelsAction")));
    QVERIFY(envelopes->applyLabels({{QStringLiteral("mode"), QStringLiteral("merge")}, {QStringLiteral("product"), QStringLiteral("5160")}}));
    QCOMPARE(mm->kind(), QStringLiteral("labels"));
    QCOMPARE(scene.doc->textDocument()->toPlainText().count(QStringLiteral("«Next Record»")), 29);
    QTRY_COMPARE(scene.page->pageCount(), 1); // ten 1 in rows padded to the pitch: one sheet
    auto *labels = qobject_cast<QTextTable *>(scene.doc->textDocument()->rootFrame()->childFrames().value(0));
    QVERIFY(labels != nullptr);
    QTextCursor first = labels->cellAt(0, 0).firstCursorPosition();
    scene.control()->setTextCursor(first);
    mm->insertField(QStringLiteral("First Name"));
    QVERIFY(scene.host.action(QStringLiteral("writeMergeUpdateLabelsAction"))->isEnabled());
    QVERIFY(mm->updateLabels());
    QCOMPARE(Docx::mergeFieldSpans(*scene.doc->textDocument()).size(), 30 + 29);
    QTRY_COMPARE(scene.page->pageCount(), 1);
    QSignalSpy mergedSpy(&scene.host, &WriteHost::documentMade);
    QVERIFY(mm->finishToDocument({}));
    auto merged = made(mergedSpy.first());
    const QString text = merged->textDocument()->toPlainText();
    QVERIFY(text.contains(QStringLiteral("Ada")) && text.contains(QStringLiteral("Grace")) && text.contains(QStringLiteral("Katherine")));
    QCOMPARE(sections(merged->textDocument()), 0); // three records fill one sheet
    QCOMPARE(merged->textDocument()->pageCount(), 1);
}

void WriteMailingsTest::grabs()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTemporaryDir dir;
        auto *mm = merge(scene);
        QVERIFY(mm->useList(QUrl::fromLocalFile(writeCsv(dir))));
        typeLetter(scene);
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        ribbon->setProperty("currentTabId", QStringLiteral("mailings"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_mailings_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergePreviewAction")));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_preview_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeMergePreviewAction")));
        for (const auto &[action, name] : {std::pair{"writeMergeEditRecipientsAction", "recipientListDialog"},
                                           std::pair{"writeMergeAddressBlockAction", "addressBlockDialog"},
                                           std::pair{"writeLabelsAction", "labelsDialog"},
                                           std::pair{"writeEnvelopesAction", "envelopesDialog"}}) {
            QVERIFY(scene.host.actions()->trigger(QString::fromLatin1(action)));
            QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
            QVERIFY2(dialog != nullptr, name);
            QVERIFY(QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); }));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_%1_%2").arg(QLatin1String(name)).arg(width));
            QMetaObject::invokeMethod(dialog, "close");
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
        // A label sheet as Start Mail Merge ▸ Labels leaves it.
        QVERIFY(scene.host.envelopesController()->applyLabels(
            {{QStringLiteral("mode"), QStringLiteral("merge")}, {QStringLiteral("product"), QStringLiteral("5160")}}));
        ribbon->setProperty("currentTabId", QStringLiteral("mailings"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_label_sheet_%1").arg(width));
    }
}

WRITE_TEST_MAIN(WriteMailingsTest)

#include "tst_write_mailings.moc"
