// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote's content blocks: the shared layout (note_blocks.h) that the
// screen view positions widgets with and the pure page renderer prints
// with, plus the two block kinds beyond typed text. Split from
// tst_qindanote.cpp to keep both inside the line budget.

#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_page_view.h"
#include "qindanote/src/note_window.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QtTest>

using namespace QindaOffice::Apps::Notes;

class NoteBlocksTest : public QObject {
    Q_OBJECT
private slots:
    void blocksStackWithoutOverlapping();
    void checklistItemsPersistTheirState();
    void imageBlocksCarryTheirBytes();
};

void NoteBlocksTest::blocksStackWithoutOverlapping()
{
    // Three kinds of block, each as tall as it needs; the layout the SCREEN
    // uses is the layout PRINT uses, so they cannot drift apart.
    QImage image(40, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    const QJsonArray blocks{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("notes")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                    {QStringLiteral("text"), QStringLiteral("buy milk")},
                    {QStringLiteral("checked"), true}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}},
    };
    const QByteArray json =
        QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}}).toJson();

    const QList<PageBlock> laid = layoutBlocks(json, 760);
    QCOMPARE(laid.size(), 3);
    QCOMPARE(laid.at(0).kind, PageBlock::Kind::Text);
    QCOMPARE(laid.at(1).kind, PageBlock::Kind::Check);
    QCOMPARE(laid.at(2).kind, PageBlock::Kind::Image);
    QVERIFY(laid.at(1).checked);
    for (int i = 1; i < laid.size(); ++i) {
        QVERIFY2(laid.at(i).rect.top() >= laid.at(i - 1).rect.bottom(),
                 "blocks must not overlap");
    }
    // A checklist row is shorter than a text block; an image is as tall as
    // it needs within the page bound.
    QVERIFY(laid.at(1).rect.height() < laid.at(0).rect.height());
    QVERIFY(laid.at(2).rect.height() > 0);
}

void NoteBlocksTest::checklistItemsPersistTheirState()
{
    NoteWindow window;
    window.show();
    NotePage *page = window.currentPage();
    QVERIFY(page != nullptr);

    window.pageView()->addCheckItem();

    auto *box = window.pageView()->findChild<QCheckBox *>(QStringLiteral("noteCheckBox"));
    auto *label = window.pageView()->findChild<QLineEdit *>(QStringLiteral("noteCheckText"));
    QVERIFY(box != nullptr && label != nullptr);
    label->setText(QStringLiteral("call the bank"));
    emit label->textEdited(label->text());
    box->setChecked(true);

    // The state has to reach the envelope, or it is lost on the next page
    // switch — the same defect the text blocks once had.
    const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, 760);
    QCOMPARE(laid.size(), 1);
    QCOMPARE(laid.first().kind, PageBlock::Kind::Check);
    QCOMPARE(laid.first().text, QStringLiteral("call the bank"));
    QVERIFY(laid.first().checked);
}

void NoteBlocksTest::imageBlocksCarryTheirBytes()
{
    NoteWindow window;
    window.show();
    NotePage *page = window.currentPage();
    QVERIFY(page != nullptr);
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    window.pageView()->addImageBlock(bytes);

    // Bytes, not a path: a notebook page is one self-contained JSON file,
    // so a reference would break the moment the notebook moved.
    const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, 760);
    QCOMPARE(laid.size(), 1);
    QCOMPARE(laid.first().kind, PageBlock::Kind::Image);
    QCOMPARE(laid.first().imageData, bytes);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));
    NoteBlocksTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_note_blocks.moc"
