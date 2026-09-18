// SPDX-License-Identifier: GPL-3.0-or-later
// PageTextControl on its own (D-029): key events edit through
// processEvent, a mouse press lands the caret where the layout says,
// Shift+arrows select, the clipboard round-trips rich text, undo follows
// the document, and the input-method queries answer for Wayland
// text-input. Verified here before the item builds on it.

#include "qindawrite/src/page_text_control.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTextDocument>
#include <QtTest>

using QindaOffice::Apps::Write::PageTextControl;

namespace {

constexpr qreal kPageW = 794.0;
constexpr qreal kPageH = 1123.0;

QTextDocument *pagedDocument(QObject *parent)
{
    auto *doc = new QTextDocument(parent);
    doc->setUseDesignMetrics(true);
    doc->setDocumentMargin(96);
    doc->setPageSize(QSizeF(kPageW, kPageH));
    // setPageSize records an undo item (Qt quirk); geometry is not content.
    doc->clearUndoRedoStacks();
    return doc;
}

void type(PageTextControl &control, const QString &text)
{
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        control.processEvent(&press);
    }
}

void key(PageTextControl &control, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    control.processEvent(&press);
}

} // namespace

class WriteControlTest : public QObject {
    Q_OBJECT
private slots:
    void keyEventsEditThroughProcessEvent()
    {
        PageTextControl control(pagedDocument(this));
        control.setFocus(true);
        type(control, QStringLiteral("hello"));
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("hello"));
        key(control, Qt::Key_Backspace);
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("hell"));
        key(control, Qt::Key_Return);
        type(control, QStringLiteral("two"));
        QCOMPARE(control.document()->blockCount(), 2);
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("hell\ntwo"));
    }

    void mousePressLandsTheCaretAndDragSelects()
    {
        PageTextControl control(pagedDocument(this));
        type(control, QStringLiteral("alpha beta"));
        const QRectF end = control.cursorRect();
        QVERIFY(end.x() > 96);
        // A press at the very start of the line puts the caret at 0.
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(96, end.center().y()), QPointF(96, end.center().y()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(control.processEvent(&press));
        QCOMPARE(control.textCursor().position(), 0);
        // Dragging to the end selects everything on the line.
        QMouseEvent move(QEvent::MouseMove, QPointF(end.x() + 2, end.center().y()), QPointF(end.x() + 2, end.center().y()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        control.processEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end.x() + 2, end.center().y()),
                            QPointF(end.x() + 2, end.center().y()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        control.processEvent(&release);
        QCOMPARE(control.textCursor().selectedText(), QStringLiteral("alpha beta"));
    }

    void shiftArrowsSelectAndTheClipboardRoundTrips()
    {
        PageTextControl control(pagedDocument(this));
        QSignalSpy copyAvailable(&control, &PageTextControl::copyAvailable);
        type(control, QStringLiteral("bold"));
        key(control, Qt::Key_Home);
        key(control, Qt::Key_Right, Qt::ShiftModifier);
        key(control, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(control.textCursor().selectedText(), QStringLiteral("bo"));
        QVERIFY(!copyAvailable.isEmpty() && copyAvailable.last().first().toBool());
        control.copy();
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasHtml());
        key(control, Qt::Key_End);
        control.paste();
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("boldbo"));
        QVERIFY(control.canPaste());
    }

    void undoFollowsTheDocument()
    {
        PageTextControl control(pagedDocument(this));
        QSignalSpy undoAvailable(&control, &PageTextControl::undoAvailable);
        QVERIFY(!control.document()->isUndoAvailable());
        type(control, QStringLiteral("x"));
        QVERIFY(control.document()->isUndoAvailable());
        QVERIFY(!undoAvailable.isEmpty() && undoAvailable.last().first().toBool());
        control.undo();
        QCOMPARE(control.document()->toPlainText(), QString());
    }

    void inputMethodQueriesAnswer()
    {
        PageTextControl control(pagedDocument(this));
        type(control, QStringLiteral("ime"));
        const QRectF rect = control.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
        QVERIFY(rect.isValid());
        QVERIFY(rect.height() > 0);
        QCOMPARE(control.inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("ime"));
        QCOMPARE(control.inputMethodQuery(Qt::ImCursorPosition).toInt(), 3);
        // Preedit paints without entering the document; commit does.
        QList<QInputMethodEvent::Attribute> attributes;
        QInputMethodEvent preedit(QStringLiteral("ka"), attributes);
        control.processEvent(&preedit);
        QCOMPARE(control.preeditText(), QStringLiteral("ka"));
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("ime"));
        QInputMethodEvent commit(QString(), attributes);
        commit.setCommitString(QStringLiteral("か"));
        control.processEvent(&commit);
        QCOMPARE(control.document()->toPlainText(), QStringLiteral("imeか"));
    }

    void pinnedFormatAppliesToTheNextKeystroke()
    {
        PageTextControl control(pagedDocument(this));
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        control.setCurrentCharFormat(bold);
        type(control, QStringLiteral("B"));
        QCOMPARE(control.textCursor().charFormat().fontWeight(), int(QFont::Bold));
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WriteControlTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_control.moc"
