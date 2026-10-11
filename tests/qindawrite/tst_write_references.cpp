// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_test_support.h"
#include "qindawrite/src/write_references.h"
#include "qindawrite/src/write_reference_edit.h"
#include "qindawrite/src/write_toc.h"
#include "qindawrite/src/style_catalog.h"
#include "qindawrite/src/page_geometry.h"
#include "office_docx/docx_fixture.h"
#include "qindawrite/src/pdf_exporter.h"
#include "docx_reference_fields.h"
#include "docx_math.h"
#include "docx_graphics.h"
#include "qindawrite/src/write_graphics.h"
#include "office_docx/docx_roundtrip.h"
#include "support/quick_dump.h"
#include <QTextBlock>
#include <QKeyEvent>
#include <poppler-qt6.h>
using namespace QindaOffice;
using namespace WriteTest;
namespace {
QVariantMap source(const QString &tag = QStringLiteral("Rivera2026"))
{
    return {{QStringLiteral("tag"), tag}, {QStringLiteral("title"), QStringLiteral("Reliable systems")},
            {QStringLiteral("author"), QStringLiteral("Rivera, Ana")}, {QStringLiteral("year"), QStringLiteral("2026")}, {QStringLiteral("type"), QStringLiteral("Book")}};
}
void end(Scene &scene, bool paragraph = false)
{
    auto c = scene.control()->textCursor(); c.movePosition(QTextCursor::End); if (paragraph) c.insertBlock(QTextBlockFormat(), QTextCharFormat()); scene.control()->setTextCursor(c);
}
QQuickItem *part(QObject *dialog, const char *name) { return dialog->findChild<QQuickItem *>(QString::fromLatin1(name)); }
}
class WriteReferencesTest : public QObject {
    Q_OBJECT
private slots:
    void sourcesCitationsUndoRecovery();
    void captionsCrossReferencesSavePdf();
    void tableOfContentsNativeRefresh();
    void typingAfterCitation();
    void editRenameAndRenumber();
    void preservedLockedAndUnsupported();
    void referencesBesideInlineObjects_data();
    void referencesBesideInlineObjects();
    void dialogs_data();
    void dialogs();
};
void WriteReferencesTest::sourcesCitationsUndoRecovery()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController(); QVERIFY(refs);
    QVERIFY(!refs->saveSource({}, {})); QVERIFY(refs->saveSource({}, source())); QCOMPARE(refs->sources().size(), 1);
    scene.doc->textDocument()->undo(); QCOMPARE(refs->sources().size(), 0); scene.doc->textDocument()->redo(); QCOMPARE(refs->sources().size(), 1);
    QVERIFY(refs->insertCitation(QStringLiteral("Rivera2026"), QStringLiteral("9–11"))); QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("(Rivera, 2026, p. 9–11)")));
    QVERIFY(!refs->deleteSource(QStringLiteral("Rivera2026")));
    auto edited = source(); edited.insert(QStringLiteral("year"), QStringLiteral("2027")); QVERIFY(refs->saveSource(QStringLiteral("Rivera2026"), edited));
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("2027")));
    scene.doc->textDocument()->undo(); QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("2026"))); QCOMPARE(refs->sources().first().toMap().value(QStringLiteral("year")).toString(), QStringLiteral("2026"));
    scene.doc->textDocument()->redo(); QVERIFY(refs->setStyle(QStringLiteral("numeric"))); QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("[1, p. 9–11]")));
    end(scene, true); QVERIFY(refs->insertBibliography());
    auto unused = source(QStringLiteral("Unused")); QVERIFY(refs->saveSource({}, unused)); QVERIFY(refs->deleteSource(QStringLiteral("Unused")));
    const QByteArray snapshot = scene.doc->recoverySnapshot(); QVERIFY(!snapshot.isEmpty()); WriteDocument recovered; QString error;
    QVERIFY2(recovered.loadRecovery(snapshot, &error), qPrintable(error)); QCOMPARE(recovered.parts().bibliography.sources.size(), 1);
    QCOMPARE(recovered.parts().bibliography.style, QStringLiteral("numeric")); QCOMPARE(Docx::referenceSpans(*recovered.textDocument()).size(), 2);
}
void WriteReferencesTest::captionsCrossReferencesSavePdf()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController();
    QVERIFY(refs->insertCaption(QStringLiteral("Figure"), QStringLiteral("System overview")));
    QVERIFY(refs->insertCaption(QStringLiteral("Figure"), QStringLiteral("Detailed design")));
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Figure 2: Detailed design")));
    const auto targets = refs->targets(); QCOMPARE(targets.size(), 2); QString bookmark; for (const auto &target : targets) if (target.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("Detailed design"))) bookmark = target.toMap().value(QStringLiteral("bookmark")).toString(); QVERIFY(!bookmark.isEmpty());
    QVERIFY(refs->insertCrossReference(bookmark, QStringLiteral("number"), true)); QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("2"))); const QString before = scene.doc->textDocument()->toPlainText();
    QVERIFY(refs->follow(bookmark)); QVERIFY(scene.control()->textCursor().atBlockStart()); end(scene, true);
    QVERIFY(refs->insertCrossReference(bookmark, QStringLiteral("page"), true));
    QTemporaryDir dir; QString error; const auto path = dir.filePath(QStringLiteral("references.docx")); QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); QString problems; QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    WriteDocument loaded; QVERIFY2(loaded.load(path, &error), qPrintable(error)); QCOMPARE(Docx::bookmarkRanges(*loaded.textDocument()).size(), 3); QVERIFY(loaded.textDocument()->toPlainText().startsWith(before));
    const auto pdfPath = dir.filePath(QStringLiteral("references.pdf")); QVERIFY2(Apps::Write::exportDocumentPdf(loaded.textDocument(), pdfPath, &error, loaded.decoration()), qPrintable(error));
    const auto pdf = Poppler::Document::load(pdfPath); QVERIFY(pdf); const auto page = pdf->page(0); QVERIFY(page); QVERIFY(page->text(QRectF()).contains(QStringLiteral("System overview")));
    if (const auto out = qEnvironmentVariable("QQO_GRAB_DIR"); !out.isEmpty()) QVERIFY(page->renderToImage(96, 96).save(out + QStringLiteral("/qindawrite_m94_pdf.png")));
    if (!QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) { QTemporaryDir scratch; const auto lo = Test::renderWithLibreOffice(bytes, QStringLiteral("m94_captions"), scratch); QVERIFY(!lo.isEmpty()); const auto text = Test::pdfText(lo); QVERIFY(text.contains(QStringLiteral("System overview"))); const auto lines = text.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts); QCOMPARE(lines.last().trimmed(), QStringLiteral("1")); QCOMPARE(lines.at(lines.size() - 2).trimmed(), QStringLiteral("2")); }
}
void WriteReferencesTest::tableOfContentsNativeRefresh()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController();
    auto c = scene.control()->textCursor(); c.insertText(QStringLiteral("Overview")); Apps::Write::StyleCatalog().apply(c, Apps::Write::StyleCatalog::Heading1);
    c.insertBlock(); c.insertText(QStringLiteral("Details")); Apps::Write::StyleCatalog().apply(c, Apps::Write::StyleCatalog::Heading2);
    c.insertBlock(); c.insertText(QStringLiteral("Hidden level")); Apps::Write::StyleCatalog().apply(c, Apps::Write::StyleCatalog::Heading3);
    c.movePosition(QTextCursor::Start); scene.control()->setTextCursor(c);
    QVERIFY(refs->applyToc({{QStringLiteral("first"), 1}, {QStringLiteral("last"), 2}, {QStringLiteral("links"), true}, {QStringLiteral("leaders"), true}}));
    QCOMPARE(scene.doc->textDocument()->toPlainText().count(QStringLiteral("Overview")), 2); QCOMPARE(scene.doc->textDocument()->toPlainText().count(QStringLiteral("Hidden level")), 1);
    auto spans = Docx::referenceSpans(*scene.doc->textDocument()); int toc = 0, pages = 0;
    for (const auto &s : spans) { const auto type = Docx::referenceType(s.field.value(QStringLiteral("code")).toString()); if (type == u"TOC") ++toc; if (type == u"PAGEREF") ++pages; }
    QCOMPARE(toc, 1); QCOMPARE(pages, 2);
    QTemporaryDir dir; QString error; const auto path = dir.filePath(QStringLiteral("toc.docx")); QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    WriteDocument reopened; QVERIFY2(reopened.load(path, &error), qPrintable(error)); QCOMPARE(Docx::referenceSpans(*reopened.textDocument()).size(), spans.size());
    QCOMPARE(Apps::Write::tableOfContentsOptions(*reopened.textDocument()).value(QStringLiteral("last")).toInt(), 2);
    Apps::Write::PageGeometry geometry; geometry.setPageSize(reopened.pageSize());
    QCOMPARE(Apps::Write::updateTableOfContents(reopened.textDocument(), geometry), 2);
    QCOMPARE(reopened.textDocument()->toPlainText().count(QStringLiteral("Contents")), 1);
    QString schemaErrors; const auto bytes = Docx::docxBytes(*reopened.textDocument(), reopened.parts()); QVERIFY2(Test::validates(bytes, &schemaErrors), qPrintable(schemaErrors));
    if (!QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) { QTemporaryDir scratch; const auto pdf = Test::renderWithLibreOffice(bytes, QStringLiteral("m94_contents"), scratch); QVERIFY(!pdf.isEmpty()); QVERIFY(Test::pdfText(pdf).contains(QStringLiteral("Overview"))); }
    QVERIFY(QTest::qWaitForWindowExposed(scene.window)); QTest::qWait(120); Test::saveGrab(scene.window, QStringLiteral("qindawrite_m94_contents_document"));
    const auto text = scene.doc->textDocument()->toPlainText(); QVERIFY(refs->applyToc(refs->tocOptions())); QCOMPARE(scene.doc->textDocument()->toPlainText(), text);
}
void WriteReferencesTest::typingAfterCitation()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController(); QVERIFY(refs->saveSource({}, source())); QVERIFY(refs->insertCitation(QStringLiteral("Rivera2026")));
    end(scene); const int before = scene.control()->textCursor().position(); QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral(" ordinary text")); QVERIFY(scene.control()->processEvent(&event));
    const auto spans = Docx::referenceSpans(*scene.doc->textDocument()); QCOMPARE(spans.size(), 1); QCOMPARE(spans.first().end, before);
}
void WriteReferencesTest::editRenameAndRenumber()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController(); QVERIFY(refs->saveSource({}, source()));
    QVERIFY(refs->insertCitation(QStringLiteral("Rivera2026"))); QVERIFY(refs->editCitation(QStringLiteral("Rivera2026"), QStringLiteral("42")));
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("p. 42")));
    auto changed = source(QStringLiteral("RiveraNew")); QVERIFY(refs->saveSource(QStringLiteral("Rivera2026"), changed));
    QVERIFY(Docx::referenceSpans(*scene.doc->textDocument()).first().field.value(QStringLiteral("code")).toString().contains(QStringLiteral("RiveraNew")));
    end(scene, true); QVERIFY(refs->insertCaption(QStringLiteral("Figure"), QStringLiteral("First"))); QVERIFY(refs->insertCaption(QStringLiteral("Figure"), QStringLiteral("Second")));
    QString bookmark; for (const auto &value : refs->targets()) if (value.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("Second"))) bookmark = value.toMap().value(QStringLiteral("bookmark")).toString();
    QVERIFY(refs->insertCrossReference(bookmark, QStringLiteral("number"), true)); QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("2")));
    auto c = scene.doc->textDocument()->find(QStringLiteral("Figure 1: First")); QVERIFY(!c.isNull()); c.movePosition(QTextCursor::StartOfBlock); c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor); c.removeSelectedText();
    QVERIFY(refs->refresh()); QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Figure 1: Second"))); QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("1")));
    scene.doc->textDocument()->undo(); QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Figure 2: Second")));
}
void WriteReferencesTest::preservedLockedAndUnsupported()
{
    Test::DocxFixture fixture;
    fixture.body = QStringLiteral(R"(<w:p><w:fldSimple w:instr="CITATION Missing \x unknown"><w:r><w:t>Keep unsupported</w:t></w:r></w:fldSimple></w:p><w:p><w:fldSimple w:instr="CITATION Missing" w:fldLock="true"><w:r><w:t>Keep locked</w:t></w:r></w:fldSimple></w:p><w:p><w:fldSimple w:instr="TOC \f A"><w:r><w:t>Preserved custom TOC</w:t></w:r></w:fldSimple></w:p>)");
    const auto read = Test::readWord(fixture.build()); QVERIFY(read); const auto before = read->body.toPlainText();
    QCOMPARE(Apps::Write::refreshReferences(read->body, read->parts.bibliography, 1056), 0); QCOMPARE(read->body.toPlainText(), before);
    Apps::Write::PageGeometry geometry; QCOMPARE(Apps::Write::updateTableOfContents(&read->body, geometry), -2); QCOMPARE(read->body.toPlainText(), before);
    const auto bytes = Docx::docxBytes(read->body, read->parts); const auto again = Test::readWord(bytes); QVERIFY(again); QCOMPARE(again->body.toPlainText(), before);
    QCOMPARE(Docx::referenceSpans(again->body).size(), 3);
}
void WriteReferencesTest::referencesBesideInlineObjects_data()
{
    QTest::addColumn<bool>("equation"); QTest::newRow("equation") << true; QTest::newRow("graphic") << false;
}
void WriteReferencesTest::referencesBesideInlineObjects()
{
    QFETCH(bool, equation);
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *refs = scene.host.referencesController();
    QVERIFY(refs->saveSource({}, source()));
    if (equation) {
        auto c = scene.control()->textCursor();
        const QString xml = QStringLiteral("<m:oMath xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\"><m:r><m:t>x+1</m:t></m:r></m:oMath>");
        c.insertText(QString(QChar::ObjectReplacementCharacter), Docx::equationFormat(xml, QTextCharFormat()));
        scene.control()->setTextCursor(c);
    } else QVERIFY(scene.host.graphicsController()->insert(QStringLiteral("shape")));
    auto atObjectEnd = [&] { QTextCursor c(scene.doc->textDocument()); c.setPosition(1); scene.control()->setTextCursor(c); };
    auto ordinary = [](const QTextCharFormat &format) {
        return format.objectType() == QTextFormat::NoObject && !format.isImageFormat()
            && !format.hasProperty(Docx::kInlineXml) && !format.hasProperty(Docx::kInlineSerial)
            && !format.hasProperty(Docx::kGraphicKind) && !format.hasProperty(Docx::kGraphicData);
    };
    atObjectEnd(); auto changed = source(); changed.insert(QStringLiteral("title"), QStringLiteral("Updated systems"));
    QVERIFY(refs->saveSource(QStringLiteral("Rivera2026"), changed));
    QVERIFY(ordinary(scene.control()->currentCharFormat()));
    auto chosenStyle = scene.control()->currentCharFormat(); chosenStyle.setFontWeight(QFont::Bold);
    scene.control()->setCurrentCharFormat(chosenStyle);
    QVERIFY(QMetaObject::invokeMethod(scene.control(), "cursorPositionChanged"));
    QCOMPARE(scene.control()->currentCharFormat().fontWeight(), QFont::Bold); // boundary normalization retains chosen text style
    QKeyEvent firstText(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral(" after-source"));
    QVERIFY(scene.control()->processEvent(&firstText)); QVERIFY(ordinary(scene.control()->textCursor().charFormat()));
    atObjectEnd(); QVERIFY(refs->insertCitation(QStringLiteral("Rivera2026")));
    auto citation = Docx::referenceSpans(*scene.doc->textDocument()).first(); QTextCursor selected(scene.doc->textDocument());
    selected.setPosition(citation.start); selected.setPosition(citation.end, QTextCursor::KeepAnchor);
    QVERIFY(ordinary(selected.charFormat())); QTextCharFormat citationStyle; citationStyle.setFontItalic(true); selected.mergeCharFormat(citationStyle);
    scene.control()->setTextCursor(selected); QVERIFY(refs->editCitation(QStringLiteral("Rivera2026"), QStringLiteral("42")));
    citation = Docx::referenceSpans(*scene.doc->textDocument()).first(); selected.setPosition(citation.start); selected.setPosition(citation.end, QTextCursor::KeepAnchor);
    QVERIFY(selected.charFormat().fontItalic()); QVERIFY(ordinary(selected.charFormat()));
    end(scene, true); QVERIFY(refs->insertCaption(QStringLiteral("Figure"), QStringLiteral("Reference target")));
    const QString bookmark = refs->targets().first().toMap().value(QStringLiteral("bookmark")).toString();
    const auto captionRange = Docx::bookmarkRanges(*scene.doc->textDocument()).value(bookmark);
    QTextCursor caption(scene.doc->textDocument()); caption.setPosition(captionRange.start); caption.setPosition(captionRange.start + 1, QTextCursor::KeepAnchor);
    QVERIFY(caption.charFormat().fontItalic()); QVERIFY(ordinary(caption.charFormat()));
    atObjectEnd(); QVERIFY(refs->insertCrossReference(bookmark, QStringLiteral("text"), true));
    QVERIFY(ordinary(scene.control()->currentCharFormat()));
    QKeyEvent secondText(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral(" after-reference"));
    QVERIFY(scene.control()->processEvent(&secondText)); QVERIFY(ordinary(scene.control()->textCursor().charFormat()));
    atObjectEnd(); QVERIFY(refs->insertBibliography());
    for (const auto &span : Docx::referenceSpans(*scene.doc->textDocument())) {
        QTextCursor result(scene.doc->textDocument()); result.setPosition(span.start); result.setPosition(span.start + 1, QTextCursor::KeepAnchor);
        QVERIFY(ordinary(result.charFormat()));
    }
    const auto bytes = Docx::docxBytes(*scene.doc->textDocument(), scene.doc->parts()); const auto reopened = Test::readWord(bytes); QVERIFY(reopened);
    const auto text = reopened->body.toPlainText(); QVERIFY(text.contains(QStringLiteral("after-source"))); QVERIFY(text.contains(QStringLiteral("after-reference")));
    QVERIFY(text.contains(QStringLiteral("(Rivera, 2026, p. 42)"))); QVERIFY(text.contains(QStringLiteral("Updated systems")));
    QCOMPARE(Docx::referenceSpans(reopened->body).size(), 4);
    int objects = 0;
    for (auto block = reopened->body.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it)
        if (equation ? Docx::isEquation(it.fragment().charFormat()) : it.fragment().charFormat().hasProperty(Docx::kGraphicKind)) objects += it.fragment().text().count(QChar::ObjectReplacementCharacter);
    QCOMPARE(objects, 1); QString schemaErrors; QVERIFY2(Test::validates(bytes, &schemaErrors), qPrintable(schemaErrors));
}
void WriteReferencesTest::dialogs_data() { QTest::addColumn<int>("width"); QTest::newRow("1280") << 1280; QTest::newRow("520") << 520; }
void WriteReferencesTest::dialogs()
{
    QFETCH(int, width); Scene scene(QSize(width, 800)); QVERIFY2(scene.ready(), qPrintable(scene.error)); QVERIFY(QTest::qWaitForWindowExposed(scene.window)); auto *refs = scene.host.referencesController();
    QVERIFY(refs->saveSource({}, source())); QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeSourcesAction")));
    auto *sources = scene.window->findChild<QObject *>(QStringLiteral("sourcesDialog")); QVERIFY(sources); QTRY_VERIFY(sources->property("opened").toBool()); QTest::qWait(150);
    Test::saveGrab(scene.window, QStringLiteral("qindawrite_m94_sources_%1").arg(width));
    auto *fresh = part(sources, "sourceNew"); QVERIFY(fresh); QVERIFY(QMetaObject::invokeMethod(fresh, "click"));
    for (const auto &[name, text] : QList<QPair<QString, QString>>{{QStringLiteral("source_tag"), QStringLiteral("Draft")}, {QStringLiteral("source_title"), QStringLiteral("Draft title")}}) {
        QQuickItem *field = nullptr; QTRY_VERIFY((field = scene.item(name)) != nullptr); field->setProperty("text", text); QVERIFY(QMetaObject::invokeMethod(field, "textEdited"));
    }
    auto *save = part(sources, "sourceSave"); QVERIFY(save); QVERIFY(QMetaObject::invokeMethod(save, "click")); QCOMPARE(refs->sources().size(), 2);
    auto *remove = part(sources, "sourceDelete"); QVERIFY(remove); QVERIFY(QMetaObject::invokeMethod(remove, "click")); QCOMPARE(refs->sources().size(), 1);
    QVERIFY(QMetaObject::invokeMethod(sources, "close"));
    const QStringList kinds{QStringLiteral("citation"), QStringLiteral("caption"), QStringLiteral("crossReference"), QStringLiteral("toc")};
    auto *dialog = scene.window->findChild<QObject *>(QStringLiteral("referenceDialog")); QVERIFY(dialog);
    for (const auto &kind : kinds) {
        if (kind == u"toc") {
            auto c = scene.control()->textCursor(); c.insertText(QStringLiteral("Overview")); Apps::Write::StyleCatalog().apply(c, Apps::Write::StyleCatalog::Heading1);
            c.insertBlock(); c.insertText(QStringLiteral("Details")); Apps::Write::StyleCatalog().apply(c, Apps::Write::StyleCatalog::Heading2);
            c.insertBlock(QTextBlockFormat(), QTextCharFormat()); scene.control()->setTextCursor(c);
        }
        refs->request(kind); QTRY_VERIFY(dialog->property("opened").toBool());
        if (kind == u"caption") { auto *text = part(dialog, "captionText"); QVERIFY(text); text->setProperty("text", QStringLiteral("System overview")); }
        QTest::qWait(150); Test::saveGrab(scene.window, QStringLiteral("qindawrite_m94_%1_%2").arg(kind).arg(width));
        auto *apply = part(dialog, "referenceApply"); QVERIFY(apply); QVERIFY(QMetaObject::invokeMethod(apply, "click")); QTRY_VERIFY(!dialog->property("opened").toBool()); end(scene, true);
    }
    auto *ribbon = scene.item(QStringLiteral("officeRibbon")); QVERIFY(ribbon); QVariant activated;
    QVERIFY(QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, activated), Q_ARG(QVariant, QStringLiteral("references")))); QVERIFY(activated.toBool()); QTest::qWait(150);
    for (const auto &id : {"writeCitationAction", "writeSourcesAction", "writeEditCitationAction", "writeBibliographyAction", "writeCaptionAction", "writeCrossReferenceAction", "writeRefreshReferencesAction", "writeFollowReferenceAction", "writeTocOptionsAction"}) {
        auto *button = scene.item(QStringLiteral("ribbonAction_") + QString::fromLatin1(id)); QVERIFY(button); QVERIFY(button->isVisible()); const auto rect = button->mapRectToItem(ribbon, QRectF(0, 0, button->width(), button->height())); QVERIFY(rect.left() >= -1); QVERIFY(rect.right() <= ribbon->width() + 1);
    }
    Test::saveGrab(scene.window, QStringLiteral("qindawrite_m94_ribbon_%1").arg(width));
}
WRITE_TEST_MAIN(WriteReferencesTest)
#include "tst_write_references.moc"
