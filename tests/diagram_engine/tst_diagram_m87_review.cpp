// SPDX-License-Identifier: GPL-3.0-or-later
// M87 (D-095) page tools: layers (Visio's rules for showing, printing,
// locking, glue and colour; removing a layer renumbers), hyperlinks to
// pages, comments with replies, background pages (chains, cycles refused,
// painted under the page, cleared when removed), Auto Size by paper tiles,
// printing across sheets (fit to N × M, a print zoom, the M50 default),
// and the .qdiagram file keeping all of it — while a drawing using none of
// it keeps the exact JSON M65 wrote.

#include "diagram_fixture.h"

#include "drawing_io.h"
#include "export.h"
#include "layers.h"
#include "painter.h"
#include "print_tiles.h"

#include <QImage>
#include <QPainter>
#include <QTimeZone>

using namespace DiagramTest;

namespace {

QColor pixel(const Page &page, QPointF at, const QVector<const Page *> &backgrounds = {}, bool printing = false)
{
    QImage image(page.size.toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    PaintOptions options;
    options.printing = printing;
    options.backgrounds = backgrounds;
    paintPage(&painter, page, options);
    painter.end();
    return image.pixelColor(at.toPoint());
}

} // namespace

class DiagramM87ReviewTest final : public QObject {
    Q_OBJECT
private slots:
    void layersFollowVisiosRules()
    {
        Fixture f;
        Page &p = f.page();
        const int flow = Ops::addLayer(p, QStringLiteral("Flow"));
        const int notes = Ops::addLayer(p, QStringLiteral("Notes"));
        QCOMPARE(Ops::addLayer(p, QStringLiteral("Flow")), 2); // made unique
        QCOMPARE(p.layers.at(2).name, QStringLiteral("Flow 2"));
        QVERIFY(Ops::removeLayer(p, 2));
        p.layers[notes].active = true;
        const QString a = f.shape("process", 200, 200, "A"); // goes on the active layer
        QCOMPARE(p.shape(a)->layers, QList<int>{notes});
        const QString b = f.shape("process", 500, 200, "B");
        QVERIFY(Ops::assignLayers(p, {b}, {flow, notes, 7})); // unknown indexes dropped
        QCOMPARE(p.shape(b)->layers, (QList<int>{flow, notes}));
        // Hidden only when every layer is hidden; locked when any is.
        Layer hidden = p.layers.at(notes);
        hidden.visible = false;
        QVERIFY(Ops::setLayer(p, notes, hidden));
        QVERIFY(!itemVisible(p, a));
        QVERIFY(itemVisible(p, b));
        QCOMPARE(hiddenItems(p), QStringList{a});
        QCOMPARE(Ops::shapeAt(p, QPointF(200, 200)), QString()); // nothing to pick
        QCOMPARE(pixel(p, QPointF(200, 200)), QColor(Qt::white));  // not painted
        Layer locked = p.layers.at(flow);
        locked.locked = true;
        QVERIFY(Ops::setLayer(p, flow, locked));
        QVERIFY(itemLocked(p, b));
        QVERIFY(!Ops::moveItems(p, {b}, QPointF(10, 0)));
        QCOMPARE(Ops::removeItems(p, {b}), 0);
        // Print: off paper only when no layer of the item prints.
        Layer screenOnly = p.layers.at(flow);
        screenOnly.locked = false;
        screenOnly.print = false;
        screenOnly.color = QColor(255, 0, 0);
        QVERIFY(Ops::setLayer(p, flow, screenOnly));
        hidden.visible = true;
        hidden.print = false;
        QVERIFY(Ops::setLayer(p, notes, hidden));
        p.shape(b)->style.lineWidth = 6; // a line wide enough to fill whole pixels
        const QPointF edge = p.shape(b)->rect.topLeft() + QPointF(p.shape(b)->rect.width() / 2, -1);
        QCOMPARE(pixel(p, edge).red(), 255); // the layer colour on the line
        QVERIFY(pixel(p, edge).green() < 80);
        QCOMPARE(pixel(p, edge, {}, true), QColor(Qt::white)); // no layer of B prints
        // Glue: a layer that refuses glue takes no connector ends.
        Layer noGlue = p.layers.at(flow);
        noGlue.glue = false;
        QVERIFY(Ops::setLayer(p, flow, noGlue));
        QVERIFY(!layersGlue(p, {flow}));
        QVERIFY(layersGlue(p, {flow, notes}));
        // Removing a layer renumbers what comes after it.
        QVERIFY(Ops::removeLayer(p, flow));
        QCOMPARE(p.shape(b)->layers, QList<int>{0});
        QCOMPARE(p.layers.first().name, QStringLiteral("Notes"));
        QVERIFY(!Ops::setLayer(p, 0, Layer{}));                     // a blank name is refused
    }

    void linksAndComments()
    {
        Fixture f;
        Ops::addPage(f.drawing, 1, QStringLiteral("Details"));
        const QString a = f.shape("process", 200, 200, "A");
        QVERIFY(Ops::setLinks(f.page(), {a}, {{QStringLiteral("Spec"), QString(), QStringLiteral("Details")},
                                             {QString(), QString(), QString()}, // empty: dropped
                                             {QStringLiteral("Site"), QStringLiteral(" https://example.org "), {}}}));
        QCOMPARE(f.page().shape(a)->links.size(), 2);
        QCOMPARE(f.page().shape(a)->links.at(1).address, QStringLiteral("https://example.org"));
        QCOMPARE(Ops::linkTargetPage(f.drawing, f.page().shape(a)->links.first()), f.drawing.pages.at(1).id);
        QCOMPARE(Ops::linkTargetPage(f.drawing, Hyperlink{{}, {}, QStringLiteral("details/Sheet.4")}), f.drawing.pages.at(1).id);
        QCOMPARE(Ops::linkTargetPage(f.drawing, f.page().shape(a)->links.at(1)), QString());
        // Comments: on a shape or the page, replies in the thread.
        const QDateTime when(QDate(2026, 10, 9), QTime(9, 30), QTimeZone::UTC);
        const QString c1 = Ops::addComment(f.drawing, f.page(), a, QStringLiteral(" Check the limit "), QStringLiteral("Dana Brooks"),
                                           QStringLiteral("DB"), when);
        const QString c2 = Ops::addComment(f.drawing, f.page(), {}, QStringLiteral("Page note"), QStringLiteral("Sam"), {}, when);
        const QString r1 = Ops::addComment(f.drawing, f.page(), {}, QStringLiteral("Done in v2"), QStringLiteral("Sam"), {}, when, c1);
        QVERIFY(!c1.isEmpty() && !c2.isEmpty() && !r1.isEmpty());
        QCOMPARE(Ops::addComment(f.drawing, f.page(), a, QStringLiteral("   "), {}, {}, when), QString());
        QCOMPARE(f.page().comments.at(0).text, QStringLiteral("Check the limit"));
        QCOMPARE(f.page().comments.at(2).itemId, a); // a reply sits with its thread
        QCOMPARE(f.page().comments.at(2).parentId, c1);
        QVERIFY(Ops::setCommentDone(f.page(), r1, true)); // the whole thread
        QVERIFY(f.page().comments.at(0).done && f.page().comments.at(2).done && !f.page().comments.at(1).done);
        QVERIFY(Ops::editComment(f.page(), c2, QStringLiteral("Page note, edited"), when));
        QCOMPARE(Ops::removeComment(f.page(), c1), 2); // with its reply
        QCOMPARE(f.page().comments.size(), 1);
    }

    void backgroundPagesAndAutoSize()
    {
        Fixture f;
        const QString front = f.page().id;
        const QString back = Ops::addBackgroundPage(f.drawing, front);
        QVERIFY(f.drawing.page(back)->backgroundPage);
        QCOMPARE(f.drawing.page(front)->backPageId, back);
        QCOMPARE(f.drawing.pages.last().id, back);
        // A title block on the background page shows (and prints) behind.
        QVERIFY(Ops::addBorderAndTitle(f.drawing, front, QStringLiteral("Plan")));
        const Page *backPage = f.drawing.page(back);
        QCOMPARE(backPage->shapes.size(), 3);
        const QVector<const Page *> chain = backgroundChain(f.drawing, *f.drawing.page(front));
        QCOMPARE(chain.size(), 1);
        QVERIFY(pixel(*f.drawing.page(front), QPointF(24, 300), chain, true) != QColor(Qt::white)); // the border
        // Only a background page may stand behind, never in a cycle.
        const QString other = Ops::addPage(f.drawing, 1, QStringLiteral("Other"));
        QVERIFY(!Ops::setBackPage(f.drawing, front, other));
        QVERIFY(Ops::setBackgroundPage(f.drawing, other, true));
        QVERIFY(Ops::setBackPage(f.drawing, back, other));
        QVERIFY(!Ops::setBackPage(f.drawing, other, back)); // other → back → other
        QCOMPARE(backgroundChain(f.drawing, *f.drawing.page(front)).size(), 2); // farthest first
        QCOMPARE(backgroundChain(f.drawing, *f.drawing.page(front)).first()->id, other);
        QVERIFY(Ops::removePage(f.drawing, other));
        QVERIFY(f.drawing.page(back)->backPageId.isEmpty());
        QVERIFY(Ops::setBackgroundPage(f.drawing, back, false)); // its users lose it
        QVERIFY(f.drawing.page(front)->backPageId.isEmpty());
        // Auto Size: past the right edge the page grows by a whole sheet;
        // content above the top comes in by one.
        Page &p = f.page();
        p.size = QSizeF(816, 1056);
        p.autoSize = true;
        const QString far = f.shape("process", 900, 500, "Far");
        QVERIFY(Ops::applyAutoSize(p));
        QCOMPARE(p.size, QSizeF(816 * 2, 1056));
        QCOMPARE(p.print.paper, QSizeF(816, 1056));
        QCOMPARE(p.print.zoom, 1.0);
        const QString high = f.shape("process", 300, -40, "High");
        QVERIFY(Ops::applyAutoSize(p));
        QCOMPARE(p.shape(high)->rect.center().y(), -40.0 + 1056);
        QCOMPARE(p.size, QSizeF(816 * 2, 1056 * 2));
        QVERIFY(Ops::removeItems(p, {far, high}) == 2);
        f.shape("process", 300, 300);
        QVERIFY(Ops::applyAutoSize(p));
        QCOMPARE(p.size, QSizeF(816, 1056)); // and shrinks to what is used
    }

    void printingAcrossSheets()
    {
        Page page;
        page.size = QSizeF(1632, 1056); // two letter sheets side by side
        // M50's default: one sheet, turned to the sheet's orientation.
        QVector<PrintTile> tiles = printTiles(page, QSizeF(780, 1032), 96);
        QCOMPARE(tiles.size(), 1);
        QVERIFY(tiles.first().turned);
        // Fit to 2 × 1 sheets.
        page.print.sheetsAcross = 2;
        page.print.center = false;
        tiles = printTiles(page, QSizeF(1032, 780), 96);
        QCOMPARE(tiles.size(), 2);
        QCOMPARE(tiles.at(1).region.left(), tiles.at(0).region.right());
        // Actual size on landscape letter paper less the margins: 2 × 2
        // sheets, the drawing centred on them.
        page.print = PrintSetup{QSizeF(1056, 816), 1.0, 1, 1, true};
        const QSizeF printable(1056 - kPrintMargin * 2, 816 - kPrintMargin * 2);
        tiles = printTiles(page, printable, 96);
        QCOMPARE(tiles.size(), 4);
        QCOMPARE(tiles.first().scale, 1.0);
        QCOMPARE(tiles.first().region.topLeft(), QPointF(-192, -240));
        QCOMPARE(pageBreakTiles(page).size(), 4);
        // A 1200-dpi device scales the same tiles.
        QCOMPARE(printTiles(page, printable * 12.5, 1200).size(), 4);
        // A page that fits on one sheet at actual size prints on one.
        page.size = QSizeF(900, 700);
        QCOMPARE(printTiles(page, printable, 96).size(), 1);
        page.size = QSizeF(1632, 1056);
        page.print = PrintSetup{};
        QVERIFY(pageBreakTiles(page).isEmpty());
    }

    void fileKeepsEverything()
    {
        Fixture f;
        Page &p = f.page();
        const QString a = f.shape("process", 200, 200, "A");
        const QString lane = f.shape("laneHorizontal", 400, 400, "Lane");
        Ops::addLayer(p, QStringLiteral("Flow"));
        p.layers[0].color = QColor(10, 20, 30, 128);
        p.layers[0].print = false;
        Ops::assignLayers(p, {a}, {0});
        Ops::setLinks(p, {a}, {{QStringLiteral("Home"), QStringLiteral("https://example.org"), {}}});
        Ops::addComment(f.drawing, p, a, QStringLiteral("Why?"), QStringLiteral("Ann"), QStringLiteral("A"),
                        QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC));
        p.shape(lane)->containerResize = ContainerResize::Fit;
        p.shape(lane)->containerMargin = 20;
        p.shape(a)->dataLink = DataLink{QStringLiteral("r9"), QStringLiteral("#2")};
        p.lineJumps = true;
        p.jumpStyle = JumpStyle::Gap;
        p.jumpOn = JumpOn::Vertical;
        p.autoSize = true;
        p.print = PrintSetup{QSizeF(816, 1056), 0, 2, 3, false};
        const QString back = Ops::addBackgroundPage(f.drawing, p.id);
        DataRecordset r;
        r.id = QStringLiteral("r9");
        r.name = QStringLiteral("Staff");
        r.source = QStringLiteral("/tmp/staff.xlsx");
        r.columns = {QStringLiteral("Name"), QStringLiteral("Phone")};
        r.rows = {{QStringLiteral("Ann"), QStringLiteral("1")}, {QStringLiteral("Bo"), QStringLiteral("2")}};
        r.keyColumn = -1;
        r.refreshed = QDateTime(QDate(2026, 3, 4), QTime(5, 6), QTimeZone::UTC);
        f.drawing.recordsets.append(r);
        Drawing back2;
        QString error;
        QVERIFY2(drawingFromJson(drawingToJson(f.drawing), &back2, &error), qPrintable(error));
        QVERIFY(back2 == f.drawing);
        QVERIFY(back2.page(back)->backgroundPage);
        // Unknown members of a layer and a comment survive (newer files).
        QByteArray json = drawingToJson(f.drawing);
        json.replace("\"glue\": true", "\"glue\": true, \"future\": 7");
        QVERIFY(drawingFromJson(json, &back2, &error));
        QCOMPARE(back2.pages.first().layers.first().extra.value(QStringLiteral("future")).toInt(), 7);
        QVERIFY(drawingToJson(back2).contains("\"future\": 7"));
        // A drawing without M87 features writes none of their members.
        Fixture plain;
        plain.shape("process", 100, 100, "x");
        const QByteArray text = drawingToJson(plain.drawing);
        for (const char *member : {"layers", "links", "comments", "recordsets", "print", "jumpStyle", "backPage",
                                   "containerResize", "dataLink", "autoSize"}) {
            QVERIFY2(!text.contains(QByteArray("\"") + member + "\""), member);
        }
        // A pasted copy keeps only the layers and data its page and
        // drawing have.
        Fixture other;
        const QStringList pasted = Ops::paste(other.drawing, other.page(), fragmentOf(f.page(), {a}), QPointF(10, 10));
        QCOMPARE(pasted.size(), 1);
        QVERIFY(other.page().shape(pasted.first())->layers.isEmpty());
        QVERIFY(!other.page().shape(pasted.first())->dataLink.linked());
        QCOMPARE(other.page().shape(pasted.first())->links.size(), 1);
    }
};

QTEST_MAIN(DiagramM87ReviewTest)
#include "tst_diagram_m87_review.moc"
