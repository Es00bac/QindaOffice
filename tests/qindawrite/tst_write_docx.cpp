// SPDX-License-Identifier: GPL-3.0-or-later
// M60 gates for QindaWrite's Word interchange (D-068): WriteDocument opens
// and saves .docx natively with its page layout, rich headers/footers,
// footnotes and pictures; crash recovery keeps all of it; pictures travel
// with the document into HTML; .rtf/.doc go through LibreOffice to .docx.
//
// QQO_WRITE_RENDER_DOCX (a file listing .docx paths) + QQO_GRAB_DIR render
// each listed document to <grab dir>/write-<n>.pdf through the real export
// path, for side-by-side review against Word or LibreOffice.

#include "../office_docx/docx_fixture.h"
#include "docx_properties.h"
#include "ooxml_package.h"
#include "qindawrite/src/format_controller.h"
#include "qindawrite/src/pdf_exporter.h"
#include "qindawrite/src/write_document.h"
#include "support/soffice_env.h"

#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QtTest>

using namespace QindaOffice;
using QindaOffice::Apps::Write::WriteDocument;

class WriteDocxTest : public QObject {
    Q_OBJECT
private slots:
    void opensWordWithLayoutHeadersAndPictures();
    void savesWordThatReopensTheSame();
    void recoveryKeepsTheWholeDocument();
    void htmlCarriesPicturesInside();
    void legacyFormatsGoThroughWord();
    void rendersListedDocuments();
};

namespace {

QString writeFixture(const QTemporaryDir &dir, const QString &name)
{
    Test::DocxFixture f;
    f.media.insert(QStringLiteral("rIdPic"), Test::pngBytes(48, 24, Qt::blue));
    f.header = QStringLiteral(
        "<w:p><w:pPr><w:jc w:val=\"right\"/></w:pPr><w:r><w:drawing><wp:inline><wp:extent cx=\"457200\" cy=\"228600\"/>"
        "<wp:docPr id=\"1\" name=\"Logo\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"logo.png\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill>"
        "<a:blip r:embed=\"rIdPic\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
        "<a:ext cx=\"457200\" cy=\"228600\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
        "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r><w:r><w:t xml:space=\"preserve\"> Fresh Start</w:t></w:r></w:p>");
    f.footer = QStringLiteral("<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Page </w:t></w:r>"
                              "<w:fldSimple w:instr=\" PAGE \"><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p>");
    f.body = Test::para(QStringLiteral("Intake form")) + Test::para(QStringLiteral("Second page"), QStringLiteral("<w:pageBreakBefore/>"))
        + QStringLiteral("<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdHeader\"/>"
                         "<w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                         "<w:pgMar w:top=\"1080\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1800\" w:header=\"540\""
                         " w:footer=\"600\" w:gutter=\"0\"/></w:sectPr>");
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(f.build());
    }
    return path;
}

bool sofficeAvailable()
{
    QProcess probe;
    Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    return probe.waitForFinished(20000) && probe.exitCode() == 0;
}

} // namespace

void WriteDocxTest::opensWordWithLayoutHeadersAndPictures()
{
    QTemporaryDir dir;
    WriteDocument doc;
    QString error;
    QVERIFY2(doc.load(writeFixture(dir, QStringLiteral("in.docx")), &error), qPrintable(error));
    const Docx::PageLayout &l = doc.pageLayout();
    QCOMPARE(l.pageSize, QSizeF(816, 1056));
    QCOMPARE(l.margins, QMarginsF(120, 72, 96, 96));
    QCOMPARE(doc.pageMargin(), 120.0);
    QCOMPARE(doc.textDocument()->pageSize(), QSizeF(816, 1056));
    QCOMPARE(doc.textDocument()->rootFrame()->frameFormat().leftMargin(), 120.0);
    QCOMPARE(doc.textDocument()->rootFrame()->frameFormat().topMargin(), 72.0);
    QVERIFY(!doc.headerIsSimple()); // a logo: richer than one plain line
    QCOMPARE(doc.headerText(), QStringLiteral("Fresh Start"));
    QCOMPARE(doc.footerText(), QStringLiteral("Page {page}"));
    QVERIFY(!doc.isDirty());
    const Apps::Write::PageDecoration decoration = doc.decoration();
    QVERIFY(!decoration.isEmpty());
    QVERIFY(decoration.headerFor(1) != nullptr);
}

void WriteDocxTest::savesWordThatReopensTheSame()
{
    QTemporaryDir dir;
    WriteDocument doc;
    QString error;
    QVERIFY(doc.load(writeFixture(dir, QStringLiteral("in.docx")), &error));
    // Edit: a picture inserted from a file, then saved as a new .docx.
    const QString picture = dir.filePath(QStringLiteral("dot.png"));
    QFile file(picture);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(Test::pngBytes(20, 20, Qt::green));
    file.close();
    const QString name = doc.addPicture(Test::pngBytes(20, 20, Qt::green));
    QVERIFY(!name.isEmpty());
    QTextCursor cursor(doc.textDocument());
    cursor.movePosition(QTextCursor::End);
    QTextImageFormat image;
    image.setName(name);
    image.setWidth(20);
    image.setHeight(20);
    cursor.insertImage(image);
    const QString out = dir.filePath(QStringLiteral("out.docx"));
    QVERIFY2(doc.save(out, &error), qPrintable(error));
    QVERIFY(!doc.isDirty());
    QFile::remove(picture); // the picture lives in the document, not on disk
    WriteDocument back;
    QVERIFY2(back.load(out, &error), qPrintable(error));
    QCOMPARE(back.pageLayout(), doc.pageLayout());
    QCOMPARE(back.textDocument()->toPlainText(), doc.textDocument()->toPlainText());
    QCOMPARE(back.footerText(), QStringLiteral("Page {page}"));
    // The inserted picture and the header's logo: one picture store per document.
    QCOMPARE(back.parts().media.size(), 2);
    QVERIFY(!back.headerIsSimple());
    // The untouched header was carried over byte for byte, logo included.
    const auto source = Ooxml::Package::load(dir.filePath(QStringLiteral("in.docx")));
    const auto saved = Ooxml::Package::load(out);
    QVERIFY(source && saved);
    QCOMPARE(saved->data(QStringLiteral("word/header1.xml")), source->data(QStringLiteral("word/header1.xml")));
}

void WriteDocxTest::recoveryKeepsTheWholeDocument()
{
    QTemporaryDir dir;
    WriteDocument doc;
    QVERIFY(doc.load(writeFixture(dir, QStringLiteral("in.docx"))));
    doc.setHeaderFooter(doc.headerText(), QStringLiteral("Confidential — {page} of {pages}"));
    const QByteArray snapshot = doc.recoverySnapshot();
    QVERIFY(snapshot.startsWith("QINDAWRITE-DOCX/1\n"));
    WriteDocument restored;
    QString error;
    QVERIFY2(restored.loadRecovery(snapshot, &error), qPrintable(error));
    QVERIFY(restored.isDirty());
    QCOMPARE(restored.pageLayout().margins, QMarginsF(120, 72, 96, 96));
    QCOMPARE(restored.footerText(), QStringLiteral("Confidential — {page} of {pages}"));
    QVERIFY(!restored.headerIsSimple());
    // Journals written before D-068 hold HTML and still restore.
    WriteDocument legacy;
    QVERIFY(legacy.loadRecovery(QByteArrayLiteral("<p>old journal</p>")));
    QCOMPARE(legacy.textDocument()->toPlainText(), QStringLiteral("old journal"));
}

void WriteDocxTest::htmlCarriesPicturesInside()
{
    QTemporaryDir dir;
    WriteDocument doc;
    const QString name = doc.addPicture(Test::pngBytes(8, 8, Qt::red));
    QTextCursor cursor(doc.textDocument());
    QTextImageFormat image;
    image.setName(name);
    cursor.insertImage(image);
    const QString path = dir.filePath(QStringLiteral("page.html"));
    QString error;
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    QFile html(path);
    QVERIFY(html.open(QIODevice::ReadOnly));
    const QByteArray bytes = html.readAll();
    QVERIFY(bytes.contains("src=\"data:image/png;base64,"));
    QVERIFY(!bytes.contains("qwmedia:"));
}

void WriteDocxTest::legacyFormatsGoThroughWord()
{
    if (!sofficeAvailable()) {
        QSKIP("LibreOffice unavailable");
    }
    QTemporaryDir dir;
    WriteDocument doc;
    QString error;
    QVERIFY(doc.load(writeFixture(dir, QStringLiteral("in.docx")), &error));
    for (const char *ext : {"rtf", "doc", "odt"}) {
        const QString path = dir.filePath(QStringLiteral("legacy.") + QLatin1String(ext));
        if (qEnvironmentVariableIsSet("QQO_KEEP_LEGACY_DOCX")) {
            QVERIFY(doc.save(qEnvironmentVariable("QQO_KEEP_LEGACY_DOCX"), &error));
        }
        QVERIFY2(doc.save(path, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(ext), error)));
        WriteDocument back;
        QVERIFY2(back.load(path, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(ext), error)));
        QVERIFY2(back.textDocument()->toPlainText().contains(QStringLiteral("Intake form")), ext);
        QCOMPARE(back.pageSize(), QSizeF(816, 1056));
        // Nothing was written beside the user's file.
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("lo"))));
    }
}

void WriteDocxTest::rendersListedDocuments()
{
    const QString list = qEnvironmentVariable("QQO_WRITE_RENDER_DOCX");
    const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
    if (list.isEmpty() || grabs.isEmpty()) {
        QSKIP("QQO_WRITE_RENDER_DOCX / QQO_GRAB_DIR unset");
    }
    QFile file(list);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    int n = 0;
    for (const QString &path : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        WriteDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        const QString pdf = QStringLiteral("%1/write-%2.pdf").arg(grabs).arg(++n);
        QVERIFY2(Apps::Write::exportDocumentPdf(doc.textDocument(), pdf, &error, doc.decoration()), qPrintable(error));
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WriteDocxTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_docx.moc"
