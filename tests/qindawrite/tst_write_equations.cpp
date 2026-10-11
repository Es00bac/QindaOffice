// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_test_support.h"
#include "docx_math.h"
#include "docx_graphics.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "docx_text.h"
#include "equation_canvas.h"
#include "math_layout.h"
#include "math_omml.h"
#include "math_linear.h"
#include "office_docx/docx_fixture.h"
#include "support/ooxml_schema.h"
#include "support/quick_dump.h"
#include "qindawrite/src/pdf_exporter.h"
#include "qindawrite/src/write_graphics.h"
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextLayout>
#include <poppler-qt6.h>
using namespace QindaOffice;
using WriteTest::Scene;
namespace {
QString formula() { return Math::writeOmmlFragment(*Math::fromLinear(QStringLiteral("(x+1)/(y^2+1)"))); }
QTextCursor firstEquation(QTextDocument *doc)
{
    for (int at = 0; at < doc->characterCount() - 1; ++at) {
        QTextCursor cursor(doc); cursor.setPosition(at);
        const auto found = Docx::equationAt(cursor);
        if (!found.isNull()) return found;
    }
    return {};
}
QObject *popup(Scene &scene) { return scene.window->findChild<QObject *>(QStringLiteral("equationDialog")); }
void apply(Scene &scene) { QVERIFY(QMetaObject::invokeMethod(popup(scene), "accept")); }
}
class WriteEquationsTest : public QObject {
    Q_OBJECT
private slots:
    void replacingGraphicWithEquationSavesAsMath()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto *graphics = scene.host.graphicsController(); QVERIFY(graphics);
        QVERIFY(graphics->insert(QStringLiteral("chart")));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction")));
        QVERIFY(scene.host.equations()->convert(QStringLiteral("x+1"), false)); apply(scene);
        const auto equation = firstEquation(scene.doc->textDocument()); QVERIFY(!equation.isNull());
        QVERIFY(!equation.charFormat().hasProperty(Docx::kGraphicKind));
        QString error;
        const auto bytes = Docx::docxBytes(*scene.doc->textDocument(), scene.doc->parts(), {}, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        QTextDocument reopened; Docx::WordParts parts;
        QVERIFY2(Docx::readDocx(Ooxml::Package::fromData(bytes), &reopened, &parts, &error), qPrintable(error));
        QVERIFY(!firstEquation(&reopened).isNull());
    }
    void adjacentGraphicDoubleClickEditsExactObject()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction")));
        QVERIFY(scene.host.equations()->convert(QStringLiteral("x"), false)); apply(scene);
        QTextCursor afterEquation(scene.doc->textDocument());
        afterEquation.setPosition(1); scene.control()->setTextCursor(afterEquation);
        auto *graphics = scene.host.graphicsController(); QVERIFY(graphics);
        QVERIFY(graphics->insert(QStringLiteral("shape")));
        QCOMPARE(scene.doc->textDocument()->toPlainText().size(), 2);
        QSignalSpy graphicEditor(graphics, &Apps::Write::GraphicsController::dialogRequested);
        scene.control()->doubleClicked(1);
        QCOMPARE(graphicEditor.size(), 1);
        QVERIFY(!scene.host.equations()->active());
    }
    void unreadableEquationReportsAnErrorWithoutChangingDocument()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        QTextCursor cursor(scene.doc->textDocument());
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), Docx::equationFormat(QStringLiteral("<broken"), {}));
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        scene.control()->setTextCursor(cursor); scene.doc->textDocument()->clearUndoRedoStacks();
        QSignalSpy messages(&scene.host, &Quick::DocumentHost::messageRequested);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeEditEquationAction")));
        QCOMPARE(messages.count(), 1); QVERIFY(!messages.first().at(1).toString().isEmpty());
        QVERIFY(!scene.host.equations()->active());
        QVERIFY(!scene.doc->textDocument()->isUndoAvailable());
        QCOMPARE(firstEquation(scene.doc->textDocument()).charFormat().stringProperty(Docx::kInlineXml), QStringLiteral("<broken"));
    }
    void insertEditCancelAndDocumentHistory()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        QTextCursor cursor(scene.doc->textDocument()); cursor.insertText(QStringLiteral("Before "));
        scene.control()->setTextCursor(cursor); scene.doc->textDocument()->clearUndoRedoStacks();
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction")));
        QTRY_VERIFY(popup(scene)->property("opened").toBool());
        auto *draft = scene.host.equations();
        QVERIFY(draft->convert(QStringLiteral("(x+1)/(y^2+1)"), false));
        QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("Before "));
        apply(scene);
        auto equation = firstEquation(scene.doc->textDocument()); QVERIFY(!equation.isNull());
        const QString saved = equation.charFormat().stringProperty(Docx::kInlineXml);
        scene.control()->undo(); QCOMPARE(scene.doc->textDocument()->toPlainText(), QStringLiteral("Before "));
        scene.control()->redo(); QVERIFY(!firstEquation(scene.doc->textDocument()).isNull());
        scene.control()->setTextCursor(firstEquation(scene.doc->textDocument()));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeEditEquationAction")));
        QVERIFY(draft->convert(QStringLiteral("x^3"), false));
        QVERIFY(QMetaObject::invokeMethod(popup(scene), "reject"));
        QCOMPARE(firstEquation(scene.doc->textDocument()).charFormat().stringProperty(Docx::kInlineXml), saved);
        scene.host.actions()->trigger(QStringLiteral("writeEditEquationAction"));
        QVERIFY(draft->convert(QStringLiteral("\\sqrt{x}"), true)); apply(scene);
        QVERIFY(firstEquation(scene.doc->textDocument()).charFormat().stringProperty(Docx::kInlineXml) != saved);
        scene.control()->undo(); QCOMPARE(firstEquation(scene.doc->textDocument()).charFormat().stringProperty(Docx::kInlineXml), saved);
    }
    void templatesAndDraftHistory()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        auto *draft = scene.host.equations(); QCOMPARE(draft->templateIds().size(), 23);
        for (const auto &id : draft->templateIds()) {
            QVERIFY(draft->begin());
            const QString empty = draft->resultXml();
            QVERIFY(scene.host.actions()->trigger(id));
            QVERIFY(draft->resultXml() != empty);
            QVERIFY(Math::readOmmlFragment(draft->resultXml()));
            QVERIFY(draft->editor()->insertText(QStringLiteral("x")));
            QVERIFY(draft->editor()->undo()); QVERIFY(draft->editor()->undo());
            QCOMPARE(draft->resultXml(), empty);
            QVERIFY(draft->editor()->redo());
            draft->cancel();
        }
        QVERIFY(scene.doc->textDocument()->isEmpty());
    }
    void unknownOmmlSurvivesAndReopens()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        const QString xml = QString::fromUtf8("<m:oMath xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\" xmlns:cx=\"urn:qinda:test\" cx:keep=\"yes\"><m:r><m:t>x</m:t></m:r><cx:future cx:token=\"retain\"/></m:oMath>");
        Test::DocxFixture fixture; fixture.body = QStringLiteral("<w:p>%1</w:p>").arg(xml);
        QString error; QVERIFY2(scene.doc->loadDocxBytes(fixture.build(), &error), qPrintable(error));
        scene.control()->setTextCursor(firstEquation(scene.doc->textDocument()));
        scene.host.actions()->trigger(QStringLiteral("writeEditEquationAction"));
        QCOMPARE(scene.host.equations()->resultXml(), firstEquation(scene.doc->textDocument()).charFormat().stringProperty(Docx::kInlineXml));
        QVERIFY(scene.host.equations()->editor()->insertText(QStringLiteral("a"))); apply(scene);
        const auto bytes = Docx::docxBytes(*scene.doc->textDocument(), scene.doc->parts(), {}, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto package = Ooxml::Package::fromData(bytes);
        QVERIFY(package->data(QStringLiteral("word/document.xml")).contains("retain"));
        QTextDocument reopened; Docx::WordParts parts;
        QVERIFY(Docx::readDocx(package, &reopened, &parts, &error));
        QVERIFY(!firstEquation(&reopened).isNull());
        QVERIFY(firstEquation(&reopened).charFormat().stringProperty(Docx::kInlineXml).contains(QStringLiteral("retain")));
    }
    void nativeSchemaAndRealBaseline()
    {
        QTextDocument doc; Docx::installMathLayout(&doc);
        QFont font; font.setPointSizeF(18); doc.setDefaultFont(font); doc.setTextWidth(600);
        QTextCursor cursor(&doc); cursor.insertText(QStringLiteral("x = "));
        QTextCharFormat format; format.setFontPointSize(18);
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), Docx::equationFormat(formula(), format));
        cursor.insertText(QStringLiteral(" + y"), format);
        doc.documentLayout()->documentSize();
        Math::LayoutOptions options; options.fontSizePt = 18; options.placeholders = false;
        const Math::EquationLayout math(*Math::readOmmlFragment(formula()), options);
        const QTextLine line = doc.begin().layout()->lineAt(0);
        QVERIFY(line.ascent() >= math.ascent() - 0.1);
        QVERIFY(line.descent() >= math.descent() - 0.1);
        QVERIFY(math.descent() > 5); // fraction's denominator is below baseline
        QString error; const auto bytes = Docx::docxBytes(doc, {}, {}, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        if (Test::schemaValidationAvailable()) {
            const auto errors = Test::validatePackage(*Ooxml::Package::fromData(bytes));
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        }
        const auto copy = Docx::instantiateHeaderFooter(doc, 1, 1);
        copy->documentLayout()->documentSize();
        QCOMPARE(copy->begin().layout()->lineAt(0).descent(), line.descent());
    }
    void importRestoresLayoutPolicy()
    {
        Test::DocxFixture fixture;
        fixture.body = QStringLiteral("<w:p>%1</w:p>").arg(formula());
        fixture.header = QStringLiteral("<w:p>%1</w:p>").arg(formula());
        QTextDocument document; document.setLayoutEnabled(false);
        Docx::WordParts parts; QString error;
        QVERIFY2(Docx::readDocx(Ooxml::Package::fromData(fixture.build()), &document, &parts, &error), qPrintable(error));
        QVERIFY(!document.isLayoutEnabled());
        QVERIFY(!firstEquation(&document).isNull());
        const auto &header = parts.headers[size_t(Docx::HeaderFooterSlot::Default)];
        QVERIFY(header.document); QVERIFY(header.document->isLayoutEnabled());
        document.setLayoutEnabled(true);
        QVERIFY(document.documentLayout()->documentSize().height() > 0);
    }
    void adjacentEquationsClipboardAndFollowingText()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        for (int i = 0; i < 2; ++i) {
            scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction"));
            QVERIFY(scene.host.equations()->convert(QStringLiteral("x^2"), false)); apply(scene);
        }
        QCOMPARE(scene.doc->textDocument()->toPlainText().size(), 2);
        scene.control()->setTextCursor(firstEquation(scene.doc->textDocument()));
        scene.control()->copy();
        QTextCursor end(scene.doc->textDocument()); end.movePosition(QTextCursor::End);
        scene.control()->setTextCursor(end); scene.control()->paste();
        QCOMPARE(scene.doc->textDocument()->toPlainText().size(), 3);
        end.movePosition(QTextCursor::End); scene.control()->setTextCursor(end);
        scene.control()->insertText(QStringLiteral(" afterward"));
        QString error;
        const auto package = Ooxml::Package::fromData(Docx::docxBytes(*scene.doc->textDocument(), scene.doc->parts(), {}, &error));
        QVERIFY2(package, qPrintable(error));
        const auto root = package->xml(QStringLiteral("word/document.xml"));
        const auto *paragraph = root->descendant(QString::fromUtf16(Ooxml::Ns::W), u"p"); QVERIFY(paragraph);
        QCOMPARE(paragraph->elements(QString::fromUtf16(Ooxml::Ns::M), u"oMath").size(), 3);
        QVERIFY(paragraph->text().contains(QStringLiteral("afterward")));
        QTextDocument loaded; Docx::WordParts parts; QVERIFY(Docx::readDocx(package, &loaded, &parts, &error));
        QCOMPARE(loaded.toPlainText(), scene.doc->textDocument()->toPlainText());
    }
    void keyboardAndLinearUndo()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction"));
        QTRY_VERIFY(popup(scene)->property("opened").toBool());
        auto *canvas = scene.item(QStringLiteral("equationCanvas")); QVERIFY(canvas);
        canvas->forceActiveFocus();
        QTest::keyClick(scene.window, Qt::Key_X); QTest::keyClick(scene.window, Qt::Key_Y);
        QCOMPARE(scene.host.equations()->linearText(), QStringLiteral("xy"));
        QTest::keyClick(scene.window, Qt::Key_Left);
        QTest::keyClick(scene.window, Qt::Key_Backspace);
        QCOMPARE(scene.host.equations()->linearText(), QStringLiteral("y"));
        QTest::keyClick(scene.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(scene.host.equations()->linearText(), QStringLiteral("xy"));
        QVERIFY(scene.doc->textDocument()->isEmpty());
        scene.host.actions()->trigger(QStringLiteral("equationFraction"));
        QTest::keyClick(scene.window, Qt::Key_A);
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QCOMPARE(scene.host.equations()->editor()->position().steps.last().arg, 1);
        QTest::keyClick(scene.window, Qt::Key_B);
        const auto *denominator = Math::slotArgument(scene.host.equations()->editor()->equation(),
                                                     scene.host.equations()->editor()->position());
        QVERIFY(denominator); QCOMPARE(Math::plainText(*denominator), QStringLiteral("b"));
        QVERIFY(scene.host.equations()->linearText().contains(QLatin1Char('/')));
        const QString before = scene.host.equations()->resultXml();
        QVERIFY(scene.host.equations()->convert(QStringLiteral("\\sqrt{z}"), true));
        scene.host.equations()->editor()->undo(); QCOMPARE(scene.host.equations()->resultXml(), before);
        scene.host.equations()->cancel();
    }
    void dialogGrabsAndPdf()
    {
        for (int width : {1280, 520}) {
            Scene scene(QSize(width, 800)); QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.host.actions()->trigger(QStringLiteral("writeInsertEquationAction"));
            QVERIFY(scene.host.equations()->convert(QStringLiteral("x=(-b+√(b^2-4ac))/(2a)"), false));
            QTRY_VERIFY(popup(scene)->property("opened").toBool()); QTest::qWait(150);
            Test::saveGrab(scene.window, QStringLiteral("write_equation_editor_%1").arg(width));
            const auto *gallery = scene.item(QStringLiteral("equationGallery")); QVERIFY(gallery);
            QVERIFY(gallery->width() <= width);
            for (const auto &id : scene.host.equations()->templateIds()) {
                const auto *button = scene.item(id); QVERIFY2(button, qPrintable(id));
                const QRectF bounds = button->mapRectToItem(gallery, button->boundingRect());
                QVERIFY2(bounds.left() >= -1 && bounds.right() <= gallery->width() + 1, qPrintable(id));
            }
            apply(scene); QTest::qWait(150);
            Test::saveGrab(scene.window, QStringLiteral("write_equation_page_%1").arg(width));
            QTemporaryDir dir; const QString path = dir.filePath(QStringLiteral("equations.pdf")); QString error;
            QVERIFY2(Apps::Write::exportDocumentPdf(scene.doc->textDocument(), path, &error), qPrintable(error));
            const auto pdf = Poppler::Document::load(path); QVERIFY(pdf); QCOMPARE(pdf->numPages(), 1);
            const auto page = pdf->page(0); QVERIFY(page); const QImage image = page->renderToImage(96, 96); QVERIFY(!image.isNull());
            int ink = 0; for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
                if (qGray(image.pixel(x, y)) < 128) ++ink;
            QVERIFY(ink > 80);
        }
    }
};
WRITE_TEST_MAIN(WriteEquationsTest)
#include "tst_write_equations.moc"
