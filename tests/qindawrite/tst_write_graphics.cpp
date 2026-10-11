// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_test_support.h"
#include "docx_graphics.h"
#include "qindawrite/src/write_graphics.h"
#include "qindawrite/src/pdf_exporter.h"
#include "support/quick_dump.h"
#include <QTextBlock>
#include <QKeyEvent>
#include <poppler-qt6.h>

using namespace QindaOffice;
using namespace WriteTest;
namespace {
QList<QTextImageFormat> graphics(const QTextDocument &doc)
{
    QList<QTextImageFormat> result;
    for (auto block = doc.begin(); block.isValid(); block = block.next()) for (auto i = block.begin(); !i.atEnd(); ++i)
        if (i.fragment().charFormat().hasProperty(Docx::kGraphicKind)) result.append(i.fragment().charFormat().toImageFormat());
    return result;
}
void paragraph(Scene &scene)
{
    auto cursor = scene.control()->textCursor(); cursor.movePosition(QTextCursor::End); cursor.insertBlock(); scene.control()->setTextCursor(cursor);
}
QQuickItem *part(QObject *dialog, const char *name) { return dialog->findChild<QQuickItem *>(QString::fromLatin1(name)); }
int coloured(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); y += 2) for (int x = 0; x < image.width(); x += 2) {
        const auto c = image.pixelColor(x, y);
        if (c.alpha() > 0 && qMax(c.red(), qMax(c.green(), c.blue())) - qMin(c.red(), qMin(c.green(), c.blue())) > 40) ++count;
    }
    return count;
}
} // namespace
class WriteGraphicsTest : public QObject {
    Q_OBJECT
private slots:
    void dialogsAndDocument_data();
    void dialogsAndDocument();
    void undoRecoverySaveAndPdf();
    void staleDialogCannotOverwrite();
    void textAfterGraphicIsOrdinaryText();
};
void WriteGraphicsTest::dialogsAndDocument_data()
{
    QTest::addColumn<int>("width"); QTest::newRow("1280") << 1280; QTest::newRow("520") << 520;
}
void WriteGraphicsTest::dialogsAndDocument()
{
    QFETCH(int, width);
    Scene scene(QSize(width, 800)); QVERIFY2(scene.ready(), qPrintable(scene.error)); QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    auto *controller = scene.host.graphicsController(); QVERIFY(controller);
    QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("graphicDialog")); QVERIFY(dialog);
    const QStringList actions{QStringLiteral("writeInsertChartAction"), QStringLiteral("writeInsertSmartArtAction"), QStringLiteral("writeInsertShapeAction")};
    const QStringList kinds{QStringLiteral("chart"), QStringLiteral("smartArt"), QStringLiteral("shape")};
    for (int i = 0; i < actions.size(); ++i) {
        if (i) paragraph(scene);
        QVERIFY(scene.host.actions()->trigger(actions.at(i)));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!scene.host.actions()->entry(QStringLiteral("writePictureAction"))->isEnabled());
        auto *text = part(dialog, "graphicText"); auto *apply = part(dialog, "graphicApply"); QVERIFY(text && apply);
        const QString changed = i == 0 ? QStringLiteral("Category\tRevenue\nJan\t12\nFeb\t18\nMar\t9")
                              : i == 1 ? QStringLiteral("Discover\nBuild\nShare") : QStringLiteral("Quarterly review");
        if (i == 1) {
            const QStringList nodes = changed.split(QLatin1Char('\n'));
            for (int n = 0; n < nodes.size(); ++n) {
                QQuickItem *node = nullptr;
                QTRY_VERIFY((node = scene.item(QStringLiteral("graphicNode%1").arg(n))) != nullptr);
                node->setProperty("text", nodes.at(n));
            }
        } else text->setProperty("text", changed);
        if (i == 0) {
            text->setProperty("text", QStringLiteral("Category\tRevenue\nJan\tbad"));
            QVERIFY(QMetaObject::invokeMethod(apply, "click"));
            QVERIFY(dialog->property("opened").toBool()); QVERIFY(!controller->error().isEmpty());
            text->setProperty("text", changed);
        }
        if (i == 2) {
            auto *fill = part(dialog, "graphicFill"); QVERIFY(fill); fill->setProperty("text", QStringLiteral("#2e8540"));
        }
        QTest::qWait(100);
        Test::saveGrab(scene.window, QStringLiteral("qindawrite_m93_%1_%2").arg(kinds.at(i)).arg(width));
        // The Apply action must be reachable at both widths (the toolkit
        // scrolls tall bodies; it never grows the window).
        QVERIFY(apply->width() > 0);
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(graphics(*scene.doc->textDocument()).size(), i + 1);
        const auto info = controller->info();
        QCOMPARE(info.value(i == 0 ? QStringLiteral("data") : QStringLiteral("text")).toString(), changed);
    }
    auto *ribbon = scene.item(QStringLiteral("officeRibbon")); QVERIFY(ribbon);
    QVariant activated;
    QVERIFY(QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, activated), Q_ARG(QVariant, QStringLiteral("insert"))));
    QVERIFY(activated.toBool()); QTest::qWait(120);
    for (const QString &action : actions) {
        auto *button = scene.item(QStringLiteral("ribbonAction_") + action); QVERIFY(button); QVERIFY(button->isVisible());
        const auto rect = button->mapRectToItem(ribbon, QRectF(0, 0, button->width(), button->height()));
        QVERIFY(rect.right() <= ribbon->width() + 1); QVERIFY(rect.left() >= -1);
    }
    Test::saveGrab(scene.window, QStringLiteral("qindawrite_m93_document_%1").arg(width));
}
void WriteGraphicsTest::undoRecoverySaveAndPdf()
{
    Scene scene(QSize(1280, 800)); QVERIFY2(scene.ready(), qPrintable(scene.error));
    auto *controller = scene.host.graphicsController();
    QVERIFY(controller->insert(QStringLiteral("chart"))); const auto before = graphics(*scene.doc->textDocument()).first();
    QVERIFY(controller->open());
    QVERIFY(controller->apply({{QStringLiteral("data"), QStringLiteral("Category\tSales\nNorth\t32\nSouth\t14")}}));
    const auto edited = graphics(*scene.doc->textDocument()).first(); QVERIFY(before.name() != edited.name());
    scene.doc->textDocument()->undo(); QCOMPARE(graphics(*scene.doc->textDocument()).first().name(), before.name());
    scene.doc->textDocument()->redo(); QCOMPARE(graphics(*scene.doc->textDocument()).first().name(), edited.name());
    paragraph(scene); QVERIFY(controller->insert(QStringLiteral("shape")));
    paragraph(scene); QVERIFY(controller->insert(QStringLiteral("smartArt")));
    QString error; QTemporaryDir dir; const auto docx = dir.filePath(QStringLiteral("graphics.docx"));
    QVERIFY2(scene.doc->save(docx, &error), qPrintable(error));
    WriteDocument loaded; QVERIFY2(loaded.load(docx, &error), qPrintable(error)); QCOMPARE(graphics(*loaded.textDocument()).size(), 3);
    const auto snapshot = scene.doc->recoverySnapshot(); QVERIFY2(!snapshot.isEmpty(), qPrintable(error));
    WriteDocument recovered; QVERIFY2(recovered.loadRecovery(snapshot, &error), qPrintable(error));
    QCOMPARE(graphics(*recovered.textDocument()).size(), 3);
    QCOMPARE(Docx::graphicInfo(graphics(*recovered.textDocument()).first()).value(QStringLiteral("data")), Docx::graphicInfo(edited).value(QStringLiteral("data")));
    const auto path = dir.filePath(QStringLiteral("graphics.pdf"));
    QVERIFY2(Apps::Write::exportDocumentPdf(loaded.textDocument(), path, &error, loaded.decoration()), qPrintable(error));
    const auto pdf = Poppler::Document::load(path); QVERIFY(pdf); QVERIFY(pdf->numPages() > 0);
    const auto page = pdf->page(0); QVERIFY(page);
    const QImage image = page->renderToImage(96, 96); QVERIFY(coloured(image) > 1500);
    if (const QString out = qEnvironmentVariable("QQO_GRAB_DIR"); !out.isEmpty()) QVERIFY(image.save(out + QStringLiteral("/qindawrite_m93_pdf.png")));
}
void WriteGraphicsTest::staleDialogCannotOverwrite()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); auto *controller = scene.host.graphicsController();
    QVERIFY(controller->insert(QStringLiteral("shape"))); QVERIFY(controller->open());
    scene.doc->textDocument()->undo();
    QVERIFY(!controller->apply({{QStringLiteral("text"), QStringLiteral("Stale")}}));
    QVERIFY(graphics(*scene.doc->textDocument()).isEmpty());
}
void WriteGraphicsTest::textAfterGraphicIsOrdinaryText()
{
    Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error));
    for (const auto &kind : {QStringLiteral("chart"), QStringLiteral("smartArt"), QStringLiteral("shape")}) {
        auto *controller = scene.host.graphicsController(); QVERIFY(controller->insert(kind));
        auto cursor = scene.control()->textCursor(); cursor.clearSelection(); cursor.movePosition(QTextCursor::End); scene.control()->setTextCursor(cursor);
        QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral(" following text"));
        QVERIFY(scene.control()->processEvent(&event));
        paragraph(scene);
    }
    QTemporaryDir dir; QString error; const auto path = dir.filePath(QStringLiteral("after.docx"));
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    WriteDocument reopened; QVERIFY2(reopened.load(path, &error), qPrintable(error));
    QCOMPARE(graphics(*reopened.textDocument()).size(), 3);
    QCOMPARE(reopened.textDocument()->toPlainText().count(QStringLiteral(" following text")), 3);
}
WRITE_TEST_MAIN(WriteGraphicsTest)
#include "tst_write_graphics.moc"
