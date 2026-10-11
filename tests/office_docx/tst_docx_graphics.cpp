// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics.h"
#include "docx_roundtrip.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "ooxml_namespaces.h"
#include "chart_interchange.h"
#include "chart_edit.h"
#include "support/ooxml_schema.h"
#include "xlsx_bridge.h"
#include "workbook.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QtTest>

using namespace QindaOffice;
namespace {
QTextImageFormat firstGraphic(const QTextDocument &doc)
{
    for (auto block = doc.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().hasProperty(Docx::kGraphicKind)) return it.fragment().charFormat().toImageFormat();
    return {};
}
QByteArray save(const QTextDocument &doc, const Docx::WordParts &parts)
{
    QString error; const auto bytes = Docx::docxBytes(doc, parts, {}, &error);
    if (bytes.isEmpty()) qWarning().noquote() << error;
    return bytes;
}
void schema(const std::shared_ptr<const Ooxml::Package> &package)
{
    QVERIFY(package);
    if (!Test::schemaValidationAvailable()) { qInfo("schema validation unavailable"); return; }
    const auto errors = Test::validatePackage(*package);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}
QString partEnding(const Ooxml::Package &package, const QString &ending)
{
    for (const auto &name : package.partNames()) if (package.contentType(name).endsWith(ending)) return name;
    return {};
}
void replaceFirst(QTextDocument &doc, const QTextImageFormat &format)
{
    QTextCursor cursor(&doc); cursor.setPosition(0); cursor.setPosition(1, QTextCursor::KeepAnchor); cursor.setCharFormat(format);
}
} // namespace
class DocxGraphicsTest : public QObject {
    Q_OBJECT
private slots:
    void nativeKinds_data();
    void nativeKinds();
    void chartDataAndWorkbookStayTogether();
    void chartKinds_data();
    void chartKinds();
    void smartArtTextUpdatesDataAndDrawing();
    void untouchedPartsRemainExact();
    void shapeEditsAndUndoUseDistinctImages();
    void badEditsAreAtomic();
    void resizeKeepsChartPartsAndShapeGeometry();
    void diagramMultilineNodes();
    void oneEditedReferenceLeavesItsTwinAlone();
    void libreOfficeRendersNativeGraphics();
};
void DocxGraphicsTest::nativeKinds_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"chart", "smartArt", "shape"}) QTest::newRow(kind) << QString::fromLatin1(kind);
}
void DocxGraphicsTest::nativeKinds()
{
    QFETCH(QString, kind);
    QTextDocument doc; Docx::WordParts parts;
    const auto format = Docx::newGraphic(kind, doc); QVERIFY(format.hasProperty(Docx::kGraphicKind));
    QTextCursor(&doc).insertImage(format);
    auto bytes = save(doc, parts); QVERIFY(!bytes.isEmpty());
    const auto package = Ooxml::Package::fromData(bytes); schema(package);
    QTextDocument opened; Docx::WordParts loaded; QString error;
    QVERIFY2(Docx::readDocx(package, &opened, &loaded, &error), qPrintable(error));
    const auto read = firstGraphic(opened); QCOMPARE(read.stringProperty(Docx::kGraphicKind), kind);
    QCOMPARE(Docx::graphicInfo(read).value(QStringLiteral("text")), Docx::graphicInfo(format).value(QStringLiteral("text")));
    const auto image = opened.resource(QTextDocument::ImageResource, QUrl(read.name())).value<QImage>();
    QVERIFY(!image.isNull()); QVERIFY(image.width() >= int(read.width()));
    QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(QLatin1Char('\n'))));
    schema(Ooxml::Package::fromData(save(opened, loaded)));
}
void DocxGraphicsTest::chartDataAndWorkbookStayTogether()
{
    QTextDocument doc; Docx::WordParts parts;
    QTextCursor(&doc).insertImage(Docx::newGraphic(QStringLiteral("chart"), doc));
    QTextDocument opened; Docx::WordParts loaded;
    QVERIFY(Docx::readDocx(Ooxml::Package::fromData(save(doc, parts)), &opened, &loaded));
    auto format = firstGraphic(opened); QString error;
    QVERIFY2(Docx::editGraphic(format, {{QStringLiteral("data"), QStringLiteral("Category\tNorth\tSouth\nQ1\t12\t7\nQ2\t25\t\nQ3\t-8\t19")}}, opened, &error), qPrintable(error));
    replaceFirst(opened, format);
    const auto package = Ooxml::Package::fromData(save(opened, loaded)); schema(package);
    const QString chartPart = partEnding(*package, QStringLiteral("drawingml.chart+xml")); QVERIFY(!chartPart.isEmpty());
    const auto chart = Show::ChartIO::read(package, chartPart, Ooxml::defaultOfficeTheme());
    QCOMPARE(chart.series.size(), 2); QCOMPARE(chart.series.first().values, QList<qreal>({12, 25, -8}));
    const auto rel = package->firstOfType(chartPart, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/package")); QVERIFY(rel);
    const auto workbookBytes = package->data(package->targetPart(chartPart, *rel));
    schema(Ooxml::Package::fromData(workbookBytes));
    QTemporaryDir dir; const QString path = dir.filePath(QStringLiteral("data.xlsx"));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(workbookBytes), workbookBytes.size()); file.close();
    Calc::Workbook book; QVERIFY2(Calc::IO::readXlsxFile(path, &book, &error), qPrintable(error));
    const qint32 sheet = book.sheetIndex(QStringLiteral("Sheet1"));
    QVERIFY(sheet >= 0);
    QCOMPARE(Calc::toText(book.cellValue(sheet, 0, 1)), QStringLiteral("North"));
    QCOMPARE(Calc::toText(book.cellValue(sheet, 2, 1)), QStringLiteral("25"));
    QCOMPARE(Calc::toText(book.cellValue(sheet, 3, 2)), QStringLiteral("19"));
    QTextDocument again; Docx::WordParts againParts; QVERIFY(Docx::readDocx(package, &again, &againParts));
    QCOMPARE(Docx::graphicInfo(firstGraphic(again)).value(QStringLiteral("data")), Docx::graphicInfo(format).value(QStringLiteral("data")));
}
void DocxGraphicsTest::chartKinds_data()
{
    QTest::addColumn<int>("kind");
    const auto &kinds = Show::ChartEdit::kinds();
    for (int i = 0; i < kinds.size(); ++i) QTest::newRow(kinds.at(i).label) << i;
}
void DocxGraphicsTest::chartKinds()
{
    QFETCH(int, kind);
    QTextDocument doc; Docx::WordParts parts; auto format = Docx::newGraphic(QStringLiteral("chart"), doc); QString error;
    if (Show::ChartEdit::kinds().at(kind).type == u"scatter")
        QVERIFY(Docx::editGraphic(format, {{QStringLiteral("data"), QStringLiteral("X\tSeries\n1\t4\n2\t7\n3\t5")}}, doc, &error));
    QVERIFY2(Docx::editGraphic(format, {{QStringLiteral("chartType"), kind}}, doc, &error), qPrintable(error));
    QTextCursor(&doc).insertImage(format); const auto package = Ooxml::Package::fromData(save(doc, parts)); schema(package);
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(package, &opened, &loaded));
    QCOMPARE(Docx::graphicInfo(firstGraphic(opened)).value(QStringLiteral("chartType")).toInt(), kind);
}
void DocxGraphicsTest::smartArtTextUpdatesDataAndDrawing()
{
    QTextDocument doc; Docx::WordParts parts; QTextCursor(&doc).insertImage(Docx::newGraphic(QStringLiteral("smartArt"), doc));
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(Ooxml::Package::fromData(save(doc, parts)), &opened, &loaded));
    auto format = firstGraphic(opened); const auto before = format;
    QString error; QVERIFY2(Docx::editGraphic(format, {{QStringLiteral("text"), QStringLiteral("Discover\nBuild\nShare")}}, opened, &error), qPrintable(error));
    replaceFirst(opened, format);
    const auto package = Ooxml::Package::fromData(save(opened, loaded)); schema(package);
    const auto data = package->data(partEnding(*package, QStringLiteral("diagramData+xml")));
    const auto drawing = package->data(partEnding(*package, QStringLiteral("diagramDrawing+xml")));
    QVERIFY(data.contains("Discover")); QVERIFY(data.contains("Share"));
    QVERIFY(drawing.contains("Discover")); QVERIFY(drawing.contains("Share")); QVERIFY(!drawing.contains(">Plan<"));
    QTextDocument again; Docx::WordParts againParts; QVERIFY(Docx::readDocx(package, &again, &againParts));
    QCOMPARE(Docx::graphicInfo(firstGraphic(again)).value(QStringLiteral("text")).toString(), QStringLiteral("Discover\nBuild\nShare"));
    QVERIFY(format.name() != before.name());
}
void DocxGraphicsTest::untouchedPartsRemainExact()
{
    QTextDocument doc; Docx::WordParts parts; QTextCursor cursor(&doc);
    for (const auto &kind : {QStringLiteral("chart"), QStringLiteral("smartArt"), QStringLiteral("shape")}) { cursor.insertImage(Docx::newGraphic(kind, doc)); cursor.insertBlock(); }
    const auto original = Ooxml::Package::fromData(save(doc, parts));
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(original, &opened, &loaded));
    const auto saved = Ooxml::Package::fromData(save(opened, loaded));
    for (const auto &name : original->partNames()) if (name.startsWith(u"word/charts/") || name.startsWith(u"word/diagrams/") || name.startsWith(u"word/embeddings/"))
        QCOMPARE(saved->data(name), original->data(name));
}
void DocxGraphicsTest::shapeEditsAndUndoUseDistinctImages()
{
    QTextDocument doc; Docx::WordParts parts; auto format = Docx::newGraphic(QStringLiteral("shape"), doc); QTextCursor(&doc).insertImage(format);
    doc.clearUndoRedoStacks(); const QString oldName = format.name(); QString error;
    QVERIFY(Docx::editGraphic(format, {{QStringLiteral("text"), QStringLiteral("Approved")}, {QStringLiteral("preset"), QStringLiteral("ellipse")}, {QStringLiteral("fill"), QStringLiteral("#ff9900")}}, doc, &error));
    replaceFirst(doc, format); QVERIFY(oldName != format.name()); doc.undo(); QCOMPARE(firstGraphic(doc).name(), oldName);
    doc.redo(); QCOMPARE(firstGraphic(doc).name(), format.name());
    const auto package = Ooxml::Package::fromData(save(doc, parts)); schema(package);
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(package, &opened, &loaded));
    const auto info = Docx::graphicInfo(firstGraphic(opened)); QCOMPARE(info.value(QStringLiteral("text")).toString(), QStringLiteral("Approved"));
    QCOMPARE(info.value(QStringLiteral("preset")).toString(), QStringLiteral("ellipse"));
}
void DocxGraphicsTest::badEditsAreAtomic()
{
    QTextDocument doc; auto format = Docx::newGraphic(QStringLiteral("chart"), doc); const auto before = format; QString error;
    QVERIFY(!Docx::editGraphic(format, {{QStringLiteral("data"), QStringLiteral("A\tB\nC\tnot-a-number")}}, doc, &error));
    QCOMPARE(format, before); QVERIFY(!error.isEmpty());
    QVERIFY(!Docx::editGraphic(format, {{QStringLiteral("width"), -1}}, doc, &error)); QCOMPARE(format, before);
    auto diagram = Docx::newGraphic(QStringLiteral("smartArt"), doc); const auto old = diagram;
    QVERIFY(!Docx::editGraphic(diagram, {{QStringLiteral("text"), QStringLiteral("Too few nodes")}}, doc, &error)); QCOMPARE(diagram, old);
}
void DocxGraphicsTest::resizeKeepsChartPartsAndShapeGeometry()
{
    for (const auto &kind : {QStringLiteral("chart"), QStringLiteral("shape")}) {
        QTextDocument doc; Docx::WordParts parts; QTextCursor(&doc).insertImage(Docx::newGraphic(kind, doc));
        const auto original = Ooxml::Package::fromData(save(doc, parts));
        QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(original, &opened, &loaded));
        auto format = firstGraphic(opened); QString error;
        QVERIFY(Docx::editGraphic(format, {{QStringLiteral("width"), 500}, {QStringLiteral("height"), 180}}, opened, &error));
        replaceFirst(opened, format); const auto package = Ooxml::Package::fromData(save(opened, loaded)); schema(package);
        if (kind == u"chart") {
            for (const auto &part : original->partNames()) if (part.startsWith(u"word/charts/") || part.startsWith(u"word/embeddings/")) QCOMPARE(package->data(part), original->data(part));
        } else {
            const auto body = package->xml(package->mainPart());
            const auto *wsp = body->descendant(QString::fromUtf16(Ooxml::Ns::WPS), u"wsp"); QVERIFY(wsp);
            const auto *extent = wsp->descendant(QString::fromUtf16(Ooxml::Ns::A), u"ext"); QVERIFY(extent);
            QCOMPARE(extent->attr(u"cx").toLongLong(), qint64(500 * 9525)); QCOMPARE(extent->attr(u"cy").toLongLong(), qint64(180 * 9525));
        }
    }
}
void DocxGraphicsTest::diagramMultilineNodes()
{
    QTextDocument doc; Docx::WordParts parts; auto format = Docx::newGraphic(QStringLiteral("smartArt"), doc); QString error;
    const QStringList nodes{QStringLiteral("Research\nPlan"), QStringLiteral("Build"), QStringLiteral("Review\nDeliver")};
    QVERIFY(Docx::editGraphic(format, {{QStringLiteral("nodes"), nodes}}, doc, &error)); QTextCursor(&doc).insertImage(format);
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(Ooxml::Package::fromData(save(doc, parts)), &opened, &loaded));
    QCOMPARE(Docx::graphicInfo(firstGraphic(opened)).value(QStringLiteral("nodes")).toStringList(), nodes);
}
void DocxGraphicsTest::oneEditedReferenceLeavesItsTwinAlone()
{
    QTextDocument doc; Docx::WordParts parts; QTextCursor(&doc).insertImage(Docx::newGraphic(QStringLiteral("chart"), doc));
    QTextDocument opened; Docx::WordParts loaded; QVERIFY(Docx::readDocx(Ooxml::Package::fromData(save(doc, parts)), &opened, &loaded));
    auto format = firstGraphic(opened); const auto originalData = Docx::graphicInfo(format).value(QStringLiteral("data"));
    QTextCursor end(&opened); end.movePosition(QTextCursor::End); end.insertBlock(); end.insertImage(format);
    QString error; QVERIFY(Docx::editGraphic(format, {{QStringLiteral("data"), QStringLiteral("Category\tChanged\nA\t999")}}, opened, &error)); replaceFirst(opened, format);
    QTextDocument again; Docx::WordParts againParts; QVERIFY(Docx::readDocx(Ooxml::Package::fromData(save(opened, loaded)), &again, &againParts));
    QList<QTextImageFormat> all;
    for (auto b = again.begin(); b.isValid(); b = b.next()) for (auto i = b.begin(); !i.atEnd(); ++i)
        if (i.fragment().charFormat().hasProperty(Docx::kGraphicKind)) all.append(i.fragment().charFormat().toImageFormat());
    QCOMPARE(all.size(), 2);
    QCOMPARE(Docx::graphicInfo(all.last()).value(QStringLiteral("data")), originalData);
    QVERIFY(Docx::graphicInfo(all.first()).value(QStringLiteral("data")).toString().contains(QStringLiteral("999")));
}
void DocxGraphicsTest::libreOfficeRendersNativeGraphics()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) QSKIP("LibreOffice unavailable");
    QTextDocument doc; Docx::WordParts parts; QTextCursor cursor(&doc); QString error;
    auto chart = Docx::newGraphic(QStringLiteral("chart"), doc);
    QVERIFY(Docx::editGraphic(chart, {{QStringLiteral("data"), QStringLiteral("Category\tSales\nQuarterA\t12\nQuarterB\t20")}}, doc, &error));
    cursor.insertImage(chart); cursor.insertBlock();
    cursor.insertImage(Docx::newGraphic(QStringLiteral("smartArt"), doc)); cursor.insertBlock();
    cursor.insertImage(Docx::newGraphic(QStringLiteral("shape"), doc));
    QTemporaryDir scratch;
    const QString pdf = Test::renderWithLibreOffice(save(doc, parts), QStringLiteral("word_graphics_interop"), scratch);
    QVERIFY(!pdf.isEmpty()); const QString text = Test::pdfText(pdf);
    QVERIFY2(text.contains(QStringLiteral("QuarterA")), "External renderer lost chart category labels");
    QVERIFY2(text.contains(QStringLiteral("Plan")) && text.contains(QStringLiteral("Deliver")), "External renderer lost SmartArt node text");
    QVERIFY2(text.contains(QStringLiteral("Shape text")), "External renderer lost shape text");
}
QTEST_MAIN(DocxGraphicsTest)
#include "tst_docx_graphics.moc"
