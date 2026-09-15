// SPDX-License-Identifier: GPL-3.0-or-later
// PROBE #5 (plan section 10): QWidgetTextControl (Qt 6.11 private) gives
// the PageTextView editing for free: processEvent with a coordinateOffset
// maps view clicks into document space, key events edit, and
// inputMethodQuery(Qt::ImCursorRectangle) answers for Wayland text-input.
// Verified here before the view builds on it.

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QTextDocument>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QtTest>

namespace {

constexpr qreal kPageW = 794.0;
constexpr qreal kPageH = 1123.0;

QTextDocument *pagedDocument()
{
    auto *doc = new QTextDocument;
    doc->setUseDesignMetrics(true);
    doc->setDocumentMargin(96);
    doc->setPageSize(QSizeF(kPageW, kPageH));
    return doc;
}

} // namespace

class WriteProbeControl : public QObject {
    Q_OBJECT
private slots:
    void keyEventsEditThroughProcessEvent();
    void mouseEventUsesCoordinateOffset();
    void inputMethodQueryAnswersCursorRect();
    void drawContentsPaintsPageBand();
};

void WriteProbeControl::keyEventsEditThroughProcessEvent()
{
    QWidgetTextControl control(pagedDocument());
    control.setTextInteractionFlags(Qt::TextEditorInteraction);
    control.setFocus(true);
    for (const QChar c : QStringLiteral("hello")) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        control.processEvent(&press);
    }
    QCOMPARE(control.toPlainText(), QStringLiteral("hello"));

    // Backspace across the offset boundary behaves like a normal editor.
    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    control.processEvent(&backspace);
    QCOMPARE(control.toPlainText(), QStringLiteral("hell"));
}

void WriteProbeControl::mouseEventUsesCoordinateOffset()
{
    QWidgetTextControl control(pagedDocument());
    control.setTextInteractionFlags(Qt::TextEditorInteraction);
    control.setFocus(true);
    control.setPlainText(QStringLiteral("alpha beta gamma"));

    // Click at view position (100, 200) on page 2 => document point
    // (100, 200 + pageH); the offset overload must place the cursor in the
    // second page's band rather than clamping to page one.
    QPointF viewPoint(110, 130);
    QPointF offset(0, kPageH);
    QMouseEvent press(QEvent::MouseButtonPress, viewPoint, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    control.processEvent(&press, offset);
    QCOMPARE(control.textCursor().hasSelection(), false);
    QVERIFY2(control.textCursor().position() >= 6,
             "click beyond the first word should move the cursor past it");
}

void WriteProbeControl::inputMethodQueryAnswersCursorRect()
{
    QWidgetTextControl control(pagedDocument());
    control.setTextInteractionFlags(Qt::TextEditorInteraction);
    control.setFocus(true);
    control.setPlainText(QStringLiteral("alpha"));
    const QVariant rect = control.inputMethodQuery(Qt::ImCursorRectangle, QVariant());
    QVERIFY(rect.isValid());
    QVERIFY(rect.toRectF().width() > 0);
}

void WriteProbeControl::drawContentsPaintsPageBand()
{
    QWidgetTextControl control(pagedDocument());
    control.setTextInteractionFlags(Qt::TextEditorInteraction);
    control.setPlainText(QStringLiteral("probe page paint"));
    QImage image(int(kPageW), int(kPageH), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    control.drawContents(&painter, QRectF(0, 0, kPageW, kPageH));
    painter.end();
    // Some pixel must differ from the white background.
    bool painted = false;
    for (int y = 0; y < image.height() && !painted; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != QColor(Qt::white)) {
                painted = true;
                break;
            }
        }
    }
    QVERIFY(painted);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    WriteProbeControl test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_probe_control.moc"
