// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures on the page (page_picture_tool.h): a click selects one, a corner
// handle scales it with its proportions as one undo step, the Picture
// dialog's size and alt text land on it, and a Word picture resized and
// re-described keeps its own drawing (crop, name) in the saved .docx.

#include "../office_docx/docx_fixture.h"
#include "docx_properties.h"
#include "ooxml_package.h"
#include "qindawrite/src/page_picture_tool.h"
#include "qindawrite/src/page_text_control.h"
#include "qindawrite/src/page_text_item.h"
#include "qindawrite/src/write_document.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;
using Apps::Write::PageTextControl;
using Apps::Write::PageTextItem;
using Apps::Write::PictureTool;
using Apps::Write::WriteDocument;

namespace {

// "Before " + a 200 × 100 picture + " after".
int insertPicture(WriteDocument &doc)
{
    QTextCursor cursor(doc.textDocument());
    cursor.insertText(QStringLiteral("Before "));
    QTextImageFormat image;
    image.setName(doc.addPicture(Test::pngBytes(200, 100)));
    image.setWidth(200);
    image.setHeight(100);
    const int position = cursor.position();
    cursor.insertImage(image);
    cursor.insertText(QStringLiteral(" after"));
    doc.textDocument()->clearUndoRedoStacks();
    return position;
}

const char *kWordPicture =
    "<w:p><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"952500\" cy=\"476250\"/>"
    "<wp:docPr id=\"5\" name=\"Logo\" descr=\"Company logo\"/><a:graphic><a:graphicData"
    " uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"logo.png\"/>"
    "<pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdPic\"/><a:srcRect l=\"5000\" r=\"5000\"/>"
    "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
    "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"952500\" cy=\"476250\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/>"
    "</a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>";

} // namespace

class WritePictureTest : public QObject {
    Q_OBJECT
private slots:
    void cornerHandleScalesInProportion();
    void clickSelectsAndDialogApplies();
    void wordPictureKeepsItsDrawing();
};

void WritePictureTest::cornerHandleScalesInProportion()
{
    WriteDocument doc;
    PageTextControl control(doc.textDocument());
    const int position = insertPicture(doc);
    doc.textDocument()->documentLayout()->documentSize();
    PictureTool tool(&control);
    const QRectF rect = tool.pictureRect(position);
    QCOMPARE(rect.size(), QSizeF(200, 100));
    QCOMPARE(tool.pictureAt(rect.center()), position);
    QCOMPARE(tool.pictureAt(rect.topLeft() - QPointF(30, 30)), -1);
    tool.select(position);
    QCOMPARE(tool.selected(), position);
    QCOMPARE(tool.handleAt(rect.bottomRight()), 4);
    QVERIFY(tool.beginDrag(4, rect.bottomRight()));
    tool.dragTo(rect.bottomRight() + QPointF(100, 10), false);
    QVERIFY(tool.finishDrag());
    QCOMPARE(tool.pictureSize(position), QSizeF(300, 150)); // the larger scale, both ways
    // A side handle stretches one way only.
    QVERIFY(tool.beginDrag(3, tool.pictureRect(position).topRight()));
    tool.dragTo(tool.pictureRect(position).topRight() + QPointF(-100, 0), false);
    QVERIFY(tool.finishDrag());
    QCOMPARE(tool.pictureSize(position), QSizeF(200, 150));
    // Each drag is one undo step; the picture's bytes never changed.
    doc.textDocument()->undo();
    QCOMPARE(tool.pictureSize(position), QSizeF(300, 150));
    doc.textDocument()->undo();
    QCOMPARE(tool.pictureSize(position), QSizeF(200, 100));
    QCOMPARE(tool.naturalSize(position), QSizeF(200, 100));
}

void WritePictureTest::clickSelectsAndDialogApplies()
{
    WriteDocument doc;
    PageTextControl control(doc.textDocument());
    PageTextItem item;
    const int position = insertPicture(doc);
    item.attach(&doc, &control);
    item.setSize(QSizeF(item.implicitWidth(), item.implicitHeight()));
    const QRectF rect = item.pictures()->pictureRect(position);
    const QPointF view = item.docToView(rect.center());
    QMouseEvent press(QEvent::MouseButtonPress, view, view, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&item, &press);
    QCOMPARE(item.pictures()->selected(), position);
    QSignalSpy dialog(&item, &PageTextItem::pictureDialogRequested);
    QMouseEvent twice(QEvent::MouseButtonDblClick, view, view, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&item, &twice);
    QCOMPARE(dialog.count(), 1);
    QVERIFY(item.pictures()->setPicture(position, QSizeF(96, 48), QStringLiteral("A chart of sales")));
    QCOMPARE(item.pictures()->altText(position), QStringLiteral("A chart of sales"));

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("pic.docx"));
    QString error;
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    const auto package = Ooxml::Package::load(path);
    const QString body = QString::fromUtf8(package->data(package->mainPart()));
    QVERIFY(body.contains(QStringLiteral("descr=\"A chart of sales\"")));
    QVERIFY(body.contains(QStringLiteral("cx=\"914400\""))); // 96 px = 1 in
}

void WritePictureTest::wordPictureKeepsItsDrawing()
{
    QTemporaryDir dir;
    Test::DocxFixture fixture;
    fixture.media.insert(QStringLiteral("rIdPic"), Test::pngBytes(40, 20));
    fixture.body = QString::fromLatin1(kWordPicture);
    const QString source = dir.filePath(QStringLiteral("logo.docx"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(fixture.build());
    file.close();
    WriteDocument doc;
    QString error;
    QVERIFY2(doc.load(source, &error), qPrintable(error));
    PageTextControl control(doc.textDocument());
    PictureTool tool(&control);
    int position = -1;
    for (QTextBlock::iterator it = doc.textDocument()->begin().begin(); !it.atEnd(); ++it) {
        if (it.fragment().charFormat().isImageFormat()) {
            position = it.fragment().position();
        }
    }
    QVERIFY(position >= 0);
    QCOMPARE(tool.altText(position), QStringLiteral("Company logo"));
    QVERIFY(tool.setPicture(position, QSizeF(200, 100), QStringLiteral("Acme logo")));
    const QString path = dir.filePath(QStringLiteral("saved.docx"));
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    const auto package = Ooxml::Package::load(path);
    const QString body = QString::fromUtf8(package->data(package->mainPart()));
    QVERIFY(body.contains(QStringLiteral("<a:srcRect l=\"5000\" r=\"5000\"/>"))); // the crop stays
    QVERIFY(body.contains(QStringLiteral("name=\"Logo\"")));
    QVERIFY(body.contains(QStringLiteral("descr=\"Acme logo\"")));
    QCOMPARE(body.count(QStringLiteral("cx=\"1905000\"")), 2); // extent and a:ext, 200 px
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    WritePictureTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_picture.moc"
