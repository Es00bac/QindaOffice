// SPDX-License-Identifier: GPL-3.0-or-later
// In-place header and footer editing (page_story_editor.h): the page view
// switches its editing control onto the story, typing and a PAGE field land
// in the header the writer saves, Esc / Close / a double-click on the body
// return, a double-click on the margin band opens it, the first page edits
// its own slot, and Page Setup replacing the header closes the editor
// before the document it edits goes away.

#include "docx_text.h"
#include "qindawrite/src/page_story_editor.h"
#include "qindawrite/src/page_text_control.h"
#include "qindawrite/src/page_text_item.h"
#include "qindawrite/src/write_document.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;
using Apps::Write::PageTextControl;
using Apps::Write::PageTextItem;
using Apps::Write::StoryEditor;
using Apps::Write::WriteDocument;

namespace {

struct Scene {
    WriteDocument doc;
    PageTextControl control{doc.textDocument()};
    PageTextItem item;

    Scene()
    {
        QTextCursor(doc.textDocument()).insertText(QStringLiteral("Body text"));
        item.attach(&doc, &control);
        item.setSize(QSizeF(item.implicitWidth(), item.implicitHeight()));
    }
};

void send(QQuickItem *item, QEvent::Type type, const QPointF &pos)
{
    QMouseEvent event(type, pos, pos, Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(item, &event);
}

} // namespace

class WriteStoryTest : public QObject {
    Q_OBJECT
private slots:
    void headerEditedInPlaceIsSaved();
    void firstPageEditsItsOwnSlot();
    void doubleClickOpensAndEscapeCloses();
    void pageSetupReplacingTheHeaderClosesTheEditor();
};

void WriteStoryTest::headerEditedInPlaceIsSaved()
{
    Scene s;
    s.item.editHeader();
    QVERIFY(s.item.isEditingStory());
    QCOMPARE(s.item.storyLabel(), QStringLiteral("Header"));
    QTextDocument *header = s.doc.storyDocument(false, Docx::HeaderFooterSlot::Default, false);
    QVERIFY(header != nullptr);
    QCOMPARE(s.control.document(), header);
    s.control.insertText(QStringLiteral("Acme — page "));
    QTextCursor cursor = s.control.textCursor();
    s.control.setCurrentCharFormat(Docx::insertPageField(cursor, false));
    s.control.setTextCursor(cursor);
    s.control.insertText(QStringLiteral(" of "));
    cursor = s.control.textCursor();
    Docx::insertPageField(cursor, true);
    QCOMPARE(s.doc.textDocument()->toPlainText(), QStringLiteral("Body text")); // the body is untouched
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
        // The page as the view paints it, header open (for review).
        const QRectF page = s.item.geometry()->pageRect(0, s.item.width());
        QImage image(page.size().toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::lightGray);
        QPainter painter(&image);
        painter.translate(-page.topLeft());
        s.item.paint(&painter);
        painter.end();
        image.save(grabs + QStringLiteral("/write_story_header.png"));
    }

    s.item.closeStory();
    QVERIFY(!s.item.isEditingStory());
    QCOMPARE(s.control.document(), s.doc.textDocument());
    QVERIFY(s.doc.isDirty());
    QCOMPARE(s.doc.headerText(), QStringLiteral("Acme — page {page} of {pages}"));

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("story.docx"));
    QString error;
    QVERIFY2(s.doc.save(path, &error), qPrintable(error));
    WriteDocument again;
    QVERIFY2(again.load(path, &error), qPrintable(error));
    QCOMPARE(again.headerText(), QStringLiteral("Acme — page {page} of {pages}"));
    QCOMPARE(again.textDocument()->toPlainText(), QStringLiteral("Body text"));
}

void WriteStoryTest::firstPageEditsItsOwnSlot()
{
    Scene s;
    s.doc.setDifferentFirstPage(true);
    s.item.editHeader(); // the caret is on page 1
    QVERIFY(s.item.isEditingStory());
    QCOMPARE(s.item.storyLabel(), QStringLiteral("First Page Header"));
    QCOMPARE(s.control.document(), s.doc.storyDocument(false, Docx::HeaderFooterSlot::First, false));
    s.control.insertText(QStringLiteral("Cover"));
    s.item.closeStory();
    QCOMPARE(s.doc.parts().header(Docx::HeaderFooterSlot::First).document->toPlainText(), QStringLiteral("Cover"));
    QVERIFY(!s.doc.parts().header(Docx::HeaderFooterSlot::Default).document);
}

void WriteStoryTest::doubleClickOpensAndEscapeCloses()
{
    Scene s;
    const QRectF page = s.item.geometry()->pageRect(0, s.item.width());
    // In the top margin band: the header opens.
    const QPointF band = page.topLeft() + QPointF(200, 30);
    send(&s.item, QEvent::MouseButtonPress, band);
    send(&s.item, QEvent::MouseButtonRelease, band);
    send(&s.item, QEvent::MouseButtonDblClick, band);
    QVERIFY(s.item.isEditingStory());
    QCOMPARE(s.item.storyEditor()->kind(), StoryEditor::Kind::Header);
    s.control.insertText(QStringLiteral("Draft"));
    // Esc returns to the body; the header keeps what was typed.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.item, &escape);
    QVERIFY(!s.item.isEditingStory());
    QCOMPARE(s.doc.headerText(), QStringLiteral("Draft"));
    // In the bottom band: the footer; a double-click on the body closes it.
    const QPointF bottom = page.bottomLeft() + QPointF(200, -30);
    send(&s.item, QEvent::MouseButtonDblClick, bottom);
    QCOMPARE(s.item.storyEditor()->kind(), StoryEditor::Kind::Footer);
    send(&s.item, QEvent::MouseButtonDblClick, page.topLeft() + QPointF(200, 300));
    QVERIFY(!s.item.isEditingStory());
    QCOMPARE(s.control.document(), s.doc.textDocument());
}

void WriteStoryTest::pageSetupReplacingTheHeaderClosesTheEditor()
{
    Scene s;
    s.item.editHeader();
    s.control.insertText(QStringLiteral("Old"));
    s.doc.setHeaderFooter(QStringLiteral("New {page}"), QString());
    QVERIFY(!s.item.isEditingStory());
    QCOMPARE(s.control.document(), s.doc.textDocument());
    QCOMPARE(s.doc.headerText(), QStringLiteral("New {page}"));
    s.control.insertText(QStringLiteral("!")); // the control is alive on the body
    QVERIFY(s.doc.textDocument()->toPlainText().contains(QLatin1Char('!')));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    WriteStoryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_story.moc"
