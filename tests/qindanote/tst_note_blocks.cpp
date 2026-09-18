// SPDX-License-Identifier: GPL-3.0-or-later
// QindaNote's content blocks: the shared layout (note_blocks.h) that the
// page item positions with and the page renderer prints with, and the
// editor contract of the page item (tap-to-type, checklist state, image
// bytes) — all reachable without a window.

#include "note_test_support.h"

#include "qindanote/src/note_blocks.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace QindaOffice::Apps::Notes;

class NoteBlocksTest final : public QObject {
    Q_OBJECT

    static QByteArray pngBytes(const QSize &size, const QColor &color)
    {
        QImage image(size, QImage::Format_ARGB32);
        image.fill(color);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

private slots:
    void blocksStackWithoutOverlapping()
    {
        // Three kinds of block, each as tall as it needs; the layout the
        // SCREEN uses is the layout PRINT uses, so they cannot drift apart.
        const QJsonArray blocks{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("notes")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                        {QStringLiteral("text"), QStringLiteral("buy milk")},
                        {QStringLiteral("checked"), true}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                        {QStringLiteral("data"),
                         QString::fromLatin1(pngBytes(QSize(40, 20), Qt::red).toBase64())}},
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
            QVERIFY2(laid.at(i).rect.top() >= laid.at(i - 1).rect.bottom(), "blocks must not overlap");
        }
        // A checklist row is a fixed height; a text block is as tall as its
        // text, so two lines out-grow the row.
        QCOMPARE(laid.at(1).rect.height(), kCheckBlockHeight);
        const QByteArray twoLines = QJsonDocument(QJsonObject{{QStringLiteral("blocks"), QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("first line\nsecond line")}}}}}).toJson();
        QVERIFY(layoutBlocks(twoLines, 760).first().rect.height() > kCheckBlockHeight);
        QVERIFY(laid.at(2).rect.height() > 0);
    }

    void aPlacedBlockKeepsItsPosition()
    {
        // M23: a block dropped where the pen tapped carries its own rect;
        // the flow lays out around it instead of pulling it back into the
        // column.
        const QJsonArray blocks{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("floating")},
                        {QStringLiteral("rect"), QJsonArray{300, 400, 200}}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("flow")}},
        };
        const QByteArray json =
            QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}}).toJson();
        const QList<PageBlock> laid = layoutBlocks(json, 760);
        QCOMPARE(laid.size(), 2);
        QVERIFY(laid.at(0).floating);
        QCOMPARE(laid.at(0).rect.x(), 300.0);
        QCOMPARE(laid.at(0).rect.y(), 400.0);
        QCOMPARE(laid.at(0).rect.width(), 200.0);
        QVERIFY(!laid.at(1).floating);
        QVERIFY(laid.at(1).rect.y() < 400.0);
    }

    void checklistItemsPersistTheirState()
    {
        NotePageItem item;
        NotePage page;
        page.loaded = true;
        item.setPage(&page);
        item.addCheckItem();
        QCOMPARE(item.editingBlock(), 0);
        QCOMPARE(item.editingKind(), QStringLiteral("check"));
        item.updateEdit(QStringLiteral("call the bank"));
        item.commitEdit();
        QCOMPARE(item.editingBlock(), -1);
        item.toggleCheck(0);
        // The state has to reach the envelope, or it is lost on the next
        // page switch.
        const QList<PageBlock> laid = layoutBlocks(page.envelope.blocksJson, 760);
        QCOMPARE(laid.size(), 1);
        QCOMPARE(laid.first().kind, PageBlock::Kind::Check);
        QCOMPARE(laid.first().text, QStringLiteral("call the bank"));
        QVERIFY(laid.first().checked);
        // Enter on an item starts the next one; Tab nests it.
        item.insertCheckItemAfter(0);
        QCOMPARE(item.editingBlock(), 1);
        item.updateEdit(QStringLiteral("nested"));
        item.indentCheckItem(1, 1);
        item.commitEdit();
        const QList<PageBlock> two = layoutBlocks(page.envelope.blocksJson, 760);
        QCOMPARE(two.size(), 2);
        QCOMPARE(two.at(1).level, 1); // drawn indented by level * kCheckIndent
    }

    void typingIsOneUndoStepPerBlock()
    {
        NotePageItem item;
        NotePage page;
        page.loaded = true;
        item.setPage(&page);
        item.addTextBlock();
        for (const char *text : {"h", "he", "hel", "hello"}) {
            item.updateEdit(QString::fromLatin1(text));
        }
        item.commitEdit();
        QCOMPARE(layoutBlocks(page.envelope.blocksJson, 760).first().text, QStringLiteral("hello"));
        // Keystrokes merged: ONE undo empties the block's text, not one
        // letter at a time.
        item.undo();
        const QList<PageBlock> after = layoutBlocks(page.envelope.blocksJson, 760);
        QVERIFY(after.isEmpty() || after.first().text.isEmpty());
        item.redo();
        QCOMPARE(layoutBlocks(page.envelope.blocksJson, 760).first().text, QStringLiteral("hello"));
    }

    void imageBlocksCarryTheirBytes()
    {
        NotePageItem item;
        NotePage page;
        page.loaded = true;
        item.setPage(&page);
        const QByteArray bytes = pngBytes(QSize(8, 8), Qt::green);
        item.addImageBlock(bytes);
        // Bytes, not a path: a notebook page is one self-contained JSON
        // file, so a reference would break the moment the notebook moved.
        const QList<PageBlock> laid = layoutBlocks(page.envelope.blocksJson, 760);
        QCOMPARE(laid.size(), 1);
        QCOMPARE(laid.first().kind, PageBlock::Kind::Image);
        QCOMPARE(laid.first().imageData, bytes);
    }

    void tapWithTheTextToolStartsABlockWhereYouTapped()
    {
        NotePageItem item;
        NotePage page;
        page.loaded = true;
        item.setPage(&page);
        item.setTool(NoteTool::Text);
        item.tapAt(QPointF(320, 500));
        QCOMPARE(item.editingBlock(), 0);
        QCOMPARE(item.editingKind(), QStringLiteral("text"));
        QVERIFY(qAbs(item.editorRect().x() - 320.0) < 40.0);
        QVERIFY(qAbs(item.editorRect().y() - 500.0) < 40.0);
        item.updateEdit(QStringLiteral("here"));
        item.commitEdit();
        // An empty block is discarded on commit; a typed one stays where
        // the tap put it.
        QCOMPARE(item.blocks().size(), 1);
        QVERIFY(item.blocks().first().floating);
        QCOMPARE(item.blockAt(QPointF(325, 505)), 0);
        item.tapAt(QPointF(50, 50));
        QCOMPARE(item.editingBlock(), 1);
        item.cancelEdit();
        QCOMPARE(item.blocks().size(), 1);
    }
};

NOTE_TEST_MAIN(NoteBlocksTest)

#include "tst_note_blocks.moc"
