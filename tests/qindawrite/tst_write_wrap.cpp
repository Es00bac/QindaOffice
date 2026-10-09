// SPDX-License-Identifier: GPL-3.0-or-later
// Floating pictures in QindaWrite through the real window (M77, D-086):
// Wrap Text (In Line, Square, Tight, Through, Top and Bottom, Behind, In
// Front) on the selected picture, each one undo step, the picture staying
// where it was when it starts to float; Position and Wrapping (an alignment
// right puts it at the column's right edge); Move with Text / Fix Position
// on Page; dragging a floating picture to place it (one step); resizing a
// floating picture keeps it floating; the commands enabled only with a
// picture selected; the saved .docx carries wp:anchor with Word's wrap.
// QQO_GRAB_DIR keeps grabs of a square-wrapped picture at 1280 and 520 px.

#include "write_test_support.h"

#include "docx_float.h"
#include "docx_properties.h"
#include "docx_text.h"
#include "qindawrite/src/page_picture_tool.h"
#include "qindawrite/src/write_float.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"

#include <QBuffer>
#include <QImage>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextLayout>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace Docx = QindaOffice::Docx;

namespace {

// A paragraph of text with a picture at its start; the picture selected.
int pictureInText(Scene &scene)
{
    QImage image(120, 80, QImage::Format_ARGB32);
    image.fill(QColor(0x2f, 0x6f, 0xd6));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    QTextCursor c(scene.doc->textDocument());
    QTextImageFormat picture;
    picture.setName(scene.doc->addPicture(bytes));
    picture.setWidth(120);
    picture.setHeight(80);
    c.insertImage(picture);
    c.insertText(QStringLiteral("The engine's cards weave algebraic patterns as a loom weaves flowers and leaves. ").repeated(6));
    scene.page->pictures()->select(0);
    return 0;
}

Docx::PictureFloat layoutAt(Scene &scene, int position)
{
    return Docx::pictureFloat(Docx::characterFormatAt(*scene.doc->textDocument(), position));
}

const QTextFrame *frameAt(Scene &scene, int position)
{
    return qobject_cast<const QTextFrame *>(
        scene.doc->textDocument()->objectForFormat(Docx::characterFormatAt(*scene.doc->textDocument(), position)));
}

} // namespace

class WriteWrapTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void wrapTextIsOneUndoStep();
    void positionAndAnchor();
    void dragToPlace();
    void savedAsWordAnchors();
    void grabs();
};

void WriteWrapTest::wrapTextIsOneUndoStep()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(!scene.host.action(QStringLiteral("writeWrapSquareAction"))->isEnabled());
    const int at = pictureInText(scene);
    QTRY_VERIFY(scene.host.action(QStringLiteral("writeWrapSquareAction"))->isEnabled());
    QVERIFY(scene.host.action(QStringLiteral("writeWrapInlineAction"))->isChecked());
    const QRectF inlineRect = scene.page->pictures()->pictureRect(at);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeWrapSquareAction")));
    QCOMPARE(layoutAt(scene, at).wrap, QStringLiteral("square"));
    QVERIFY(frameAt(scene, at) != nullptr);
    QCOMPARE(frameAt(scene, at)->frameFormat().position(), QTextFrameFormat::FloatLeft);
    QVERIFY(scene.host.action(QStringLiteral("writeWrapSquareAction"))->isChecked());
    QVERIFY(scene.host.action(QStringLiteral("writeMoveWithTextAction"))->isChecked());
    // Where it was: Word keeps a picture in place when it starts to float.
    const QRectF squareRect = scene.page->pictures()->pictureRect(at);
    QVERIFY2(qAbs(squareRect.left() - inlineRect.left()) < 1, qPrintable(QStringLiteral("%1 %2").arg(squareRect.left()).arg(inlineRect.left())));
    QCOMPARE(scene.page->pictures()->selected(), at); // still selected
    // The text flows beside it: the first line starts right of the picture.
    const QTextBlock block = scene.doc->textDocument()->begin();
    QVERIFY(block.layout()->lineAt(0).x() >= squareRect.right() - scene.doc->pageMargin());
    scene.control()->undo();
    QCOMPARE(layoutAt(scene, at).wrap, QStringLiteral("inline"));
    QVERIFY(frameAt(scene, at) == nullptr);
    // No gap is left where the float was (docx_float_display.cpp forgetDetachedFloats).
    QCOMPARE(scene.doc->textDocument()->begin().layout()->lineAt(0).x(), 0.0);
    scene.control()->redo();
    QCOMPARE(layoutAt(scene, at).wrap, QStringLiteral("square"));
    for (const auto &[action, wrap] : {std::pair{"writeWrapTightAction", "tight"}, std::pair{"writeWrapThroughAction", "through"},
                                       std::pair{"writeWrapTopBottomAction", "topAndBottom"},
                                       std::pair{"writeWrapBehindAction", "behind"}, std::pair{"writeWrapFrontAction", "inFront"}}) {
        scene.page->pictures()->select(at);
        QVERIFY(scene.host.actions()->trigger(QString::fromLatin1(action)));
        QCOMPARE(layoutAt(scene, at).wrap, QString::fromLatin1(wrap));
    }
    // In front of the text: no room in the line, drawn over it.
    QVERIFY(frameAt(scene, at) == nullptr);
    QCOMPARE(scene.page->pictures()->pictureRect(at).size(), QSizeF(120, 80));
    QVERIFY(scene.doc->decoration().overTextPictures);
    // Resizing a floating picture keeps it floating.
    scene.page->pictures()->select(at);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeWrapSquareAction")));
    QVERIFY(scene.page->pictures()->setPicture(at, QSizeF(60, 40), QString()));
    QCOMPARE(layoutAt(scene, at).wrap, QStringLiteral("square"));
    QVERIFY(frameAt(scene, at) != nullptr);
    QCOMPARE(scene.page->pictures()->pictureRect(at).size(), QSizeF(60, 40));
}

void WriteWrapTest::positionAndAnchor()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    const int at = pictureInText(scene);
    PictureLayoutController *layout = scene.host.pictureLayoutController();
    QVERIFY(layout->applyWrap(QStringLiteral("square")));
    // An offset moves the picture's box; the box it had leaves no gap.
    QVERIFY(layout->applyLayout({{QStringLiteral("hOffset"), 40.0}}));
    const qreal lineX = scene.doc->textDocument()->begin().layout()->lineAt(0).x();
    QVERIFY2(qAbs(lineX - (40 + 120 + 12)) < 1, qPrintable(QString::number(lineX))); // offset, picture, distance
    scene.control()->undo();
    scene.page->pictures()->select(at);
    QVERIFY(layout->applyLayout({{QStringLiteral("hAlign"), QStringLiteral("right")}}));
    QCOMPARE(frameAt(scene, at)->frameFormat().position(), QTextFrameFormat::FloatRight);
    const qreal columnRight = scene.doc->pageSize().width() - scene.doc->pageLayout().margins.right();
    QVERIFY(qAbs(scene.page->pictures()->pictureRect(at).right() - columnRight) < 1);
    scene.control()->undo();
    QCOMPARE(frameAt(scene, at)->frameFormat().position(), QTextFrameFormat::FloatLeft);
    // Fix Position on Page: the vertical position is now from the page top,
    // and the picture stays where it was.
    const QRectF before = scene.page->pictures()->pictureRect(at);
    scene.page->pictures()->select(at);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeFixPositionAction")));
    QCOMPARE(layoutAt(scene, at).vRelative, QStringLiteral("page"));
    QVERIFY(scene.host.action(QStringLiteral("writeFixPositionAction"))->isChecked());
    QVERIFY(qAbs(scene.page->pictures()->pictureRect(at).top() - before.top()) < 1);
    const QVariantMap info = layout->layoutInfo();
    QCOMPARE(info.value(QStringLiteral("wrap")).toString(), QStringLiteral("square"));
    QVERIFY(!info.value(QStringLiteral("moveWithText")).toBool());
}

void WriteWrapTest::dragToPlace()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    const int at = pictureInText(scene);
    QVERIFY(scene.host.pictureLayoutController()->applyWrap(QStringLiteral("inFront")));
    PictureTool *tool = scene.page->pictures();
    tool->select(at);
    const QRectF start = tool->pictureRect(at);
    QVERIFY(tool->beginMove(start.center()));
    tool->dragTo(start.center() + QPointF(150, 60), false);
    QVERIFY(tool->finishDrag());
    const QRectF moved = tool->pictureRect(at);
    QVERIFY2(qAbs(moved.left() - start.left() - 150) < 1 && qAbs(moved.top() - start.top() - 60) < 1,
             qPrintable(QStringLiteral("%1,%2").arg(moved.left()).arg(moved.top())));
    scene.control()->undo(); // one step
    QVERIFY(qAbs(tool->pictureRect(at).left() - start.left()) < 1);
    // An inline picture is not dragged into place.
    tool->select(at);
    QVERIFY(scene.host.pictureLayoutController()->applyWrap(QStringLiteral("inline")));
    QVERIFY(!tool->beginMove(tool->pictureRect(at).center()));
}

void WriteWrapTest::savedAsWordAnchors()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    const int at = pictureInText(scene);
    QVERIFY(scene.host.pictureLayoutController()->applyWrap(QStringLiteral("tight")));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("wrapped.docx"));
    QString error;
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    const auto package = QindaOffice::Ooxml::Package::load(path);
    const QByteArray xml = package->data(QStringLiteral("word/document.xml"));
    QVERIFY2(xml.contains("<wp:anchor ") && xml.contains("<wp:wrapTight wrapText=\"bothSides\"><wp:wrapPolygon"), xml.constData());
    QVERIFY(xml.contains("<wp:positionH relativeFrom=\"column\">"));
    Scene again;
    QVERIFY2(again.ready(), qPrintable(again.error));
    QVERIFY(again.host.loadPath(path));
    QCOMPARE(Docx::pictureFloat(Docx::characterFormatAt(*again.doc->textDocument(), at)).wrap, QStringLiteral("tight"));
}

void WriteWrapTest::grabs()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        pictureInText(scene);
        QVERIFY(scene.host.pictureLayoutController()->applyWrap(QStringLiteral("square")));
        QVERIFY(scene.host.pictureLayoutController()->applyLayout({{QStringLiteral("hOffset"), 40.0}, {QStringLiteral("vOffset"), 20.0}}));
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("picture")));
        ribbon->setProperty("currentTabId", QStringLiteral("pictureFormat"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_square_picture_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writePictureLayoutAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("pictureLayoutDialog"));
        QVERIFY(dialog != nullptr);
        QVERIFY(QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); }));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m77_picture_layout_%1").arg(width));
        QMetaObject::invokeMethod(dialog, "close");
    }
}

WRITE_TEST_MAIN(WriteWrapTest)

#include "tst_write_wrap.moc"
