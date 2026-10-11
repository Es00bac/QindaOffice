// SPDX-License-Identifier: GPL-3.0-or-later
// A long Word file must keep its full scroll extent while the painted item
// stays smaller than one GPU texture. Exercise the real window at both widths.

#include "write_test_support.h"
#include "support/quick_dump.h"

#include <QImage>
#include <QTextBlockFormat>

using namespace WriteTest;

class WriteLongPagesTest final : public QObject {
    Q_OBJECT
private slots:
    void visibleTile_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("path");
        QTest::newRow("synthetic-wide") << 1280 << QString();
        QTest::newRow("synthetic-narrow") << 520 << QString();
        const QString owner = qEnvironmentVariable("QQO_PRIVATE_DOCX_PATH");
        if (!owner.isEmpty()) {
            QTest::newRow("owner-wide") << 1280 << owner;
            QTest::newRow("owner-narrow") << 520 << owner;
        }
    }

    void visibleTile()
    {
        QFETCH(int, width);
        QFETCH(QString, path);
        Scene scene(QSize(width, width == 520 ? 700 : 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        if (path.isEmpty()) {
            QTextCursor cursor(scene.doc->textDocument());
            QTextBlockFormat newPage;
            newPage.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            for (int i = 0; i < 35; ++i) {
                cursor.insertBlock(newPage);
                cursor.insertText(QStringLiteral("Page %1").arg(i + 1));
            }
        } else {
            QString error;
            QVERIFY2(scene.doc->load(path, &error), qPrintable(error));
        }
        QTRY_VERIFY(scene.page->pageCount() > 20);
        QVERIFY(scene.page->implicitHeight() > 16384);
        QVERIFY(scene.page->height() <= scene.page->maxPaintHeight());
        QQuickItem *surface = scene.item(QStringLiteral("writeDocumentSurface"));
        QVERIFY(surface != nullptr);
        QCOMPARE(surface->implicitHeight(), scene.page->implicitHeight());
        QQuickItem *scroll = scene.item(QStringLiteral("viewportScroll"));
        QVERIFY(scroll != nullptr);
        scroll->setProperty("contentY", surface->implicitHeight() * 0.55);
        QTRY_VERIFY(scene.page->y() > 0);
        const QPointF point(scene.doc->pageSize().width() / 2,
                            scene.doc->pageSize().height() * 20 + 120);
        const QPointF mapped = scene.page->viewToDoc(scene.page->docToView(point));
        QVERIFY(qAbs(mapped.y() - point.y()) < 2);
        const QImage grab = scene.window->grabWindow();
        QVERIFY(!grab.isNull());
        int paper = 0;
        for (int y = 175; y < grab.height(); y += 8)
            for (int x = 0; x < grab.width(); x += 8) {
                const QColor pixel = grab.pixelColor(x, y);
                if (pixel.red() > 245 && pixel.green() > 245 && pixel.blue() > 245) ++paper;
            }
        QVERIFY2(paper > 500, "The scrolled document did not paint paper.");
        QindaOffice::Test::saveGrab(scene.window,
            QStringLiteral("qindawrite_long_%1_%2").arg(path.isEmpty() ? QStringLiteral("synthetic")
                                                       : QStringLiteral("owner")).arg(width));
        QSignalSpy visibility(scene.page, &PageTextItem::ensureVisibleRequested);
        QTextCursor end(scene.doc->textDocument());
        end.movePosition(QTextCursor::End);
        scene.control()->setTextCursor(end);
        QVERIFY(!visibility.isEmpty());
        QVERIFY2(visibility.last().at(0).toRectF().y() > scene.page->maxPaintHeight(),
                 "The later-page caret requested tile coordinates instead of document coordinates.");
    }
};

WRITE_TEST_MAIN(WriteLongPagesTest)
#include "tst_write_long_pages.moc"
