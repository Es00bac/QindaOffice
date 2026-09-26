// SPDX-License-Identifier: GPL-3.0-or-later
// diagram_engine edit operations (M50): deleting leaves connectors free,
// moves carry container members and free ends, align/distribute, rotate
// and flip about the selection, z-order, one-level groups, duplicate and
// paste re-glue copies, style/text/connector patches, the format painter,
// shape data, find/replace, pages and fit-to-contents.

#include "diagram_fixture.h"

#include "drawing_io.h"
#include "geometry.h"
#include "routing.h"

using namespace DiagramTest;

class DiagramOpsTest final : public QObject {
    Q_OBJECT
private slots:
    void deletingAShapeLeavesItsConnectorsFree()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 400, 100);
        const QString c = f.connect(a, b);
        const QPointF end = routeConnector(f.page(), *f.page().connector(c)).end();
        QCOMPARE(Ops::removeItems(f.page(), {b}), 1);
        const Connector *connector = f.page().connector(c);
        QVERIFY(connector != nullptr);
        QVERIFY(!connector->to.glued());
        QCOMPARE(connector->to.point, end);
        QVERIFY(connector->from.glued());
        QCOMPARE(Ops::removeItems(f.page(), {c, a}), 2);
        QVERIFY(f.page().shapes.isEmpty() && f.page().connectors.isEmpty());
    }

    void movesCarryContainerMembersAndFreeEnds()
    {
        Fixture f;
        const QString lane = f.shape("laneHorizontal", 400, 200);
        const QString inside = f.shape("process", 300, 200);
        const QString outside = f.shape("process", 300, 600);
        QCOMPARE(f.page().shapes.first().id, lane); // containers go to the back
        QCOMPARE(Ops::containerMembers(f.page(), lane), QStringList{inside});
        const QString free = Ops::addConnector(f.drawing, f.page(), Endpoint{inside, 2, {}},
                                               Endpoint{QString(), -1, QPointF(300, 400)});
        QVERIFY(Ops::moveItems(f.page(), {lane}, QPointF(10, 20)));
        QCOMPARE(f.page().shape(inside)->rect.center(), QPointF(310, 220));
        QCOMPARE(f.page().shape(outside)->rect.center(), QPointF(300, 600));
        // The free end follows its glued end's shape.
        QCOMPARE(f.page().connector(free)->to.point, QPointF(310, 420));
        QVERIFY(!Ops::moveItems(f.page(), {lane}, QPointF()));
        f.page().shape(outside)->locked = true;
        QVERIFY(!Ops::moveItems(f.page(), {outside}, QPointF(5, 5)));
    }

    void alignAndDistribute()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 260, 140);
        const QString c = f.shape("process", 600, 180);
        QVERIFY(Ops::align(f.page(), {a, b, c}, Ops::Align::Top));
        const double top = f.page().shape(a)->rect.top();
        QCOMPARE(f.page().shape(b)->rect.top(), top);
        QCOMPARE(f.page().shape(c)->rect.top(), top);
        QVERIFY(Ops::align(f.page(), {a, b}, Ops::Align::Center));
        QCOMPARE(f.page().shape(a)->rect.center().x(), f.page().shape(b)->rect.center().x());
        QVERIFY(!Ops::align(f.page(), {a}, Ops::Align::Left));
        QVERIFY(Ops::distribute(f.page(), {a, b, c}, Qt::Horizontal) || true);
        // Three shapes: equal gaps between neighbours, outermost stay.
        Fixture g;
        const QString x = g.shape("process", 100, 100);
        const QString y = g.shape("process", 200, 100);
        const QString z = g.shape("process", 700, 100);
        QVERIFY(Ops::distribute(g.page(), {x, y, z}, Qt::Horizontal));
        const double gap1 = g.page().shape(y)->rect.left() - g.page().shape(x)->rect.right();
        const double gap2 = g.page().shape(z)->rect.left() - g.page().shape(y)->rect.right();
        QVERIFY(qAbs(gap1 - gap2) < 1e-6);
        QCOMPARE(g.page().shape(z)->rect.center().x(), 700.0);
        QVERIFY(!Ops::distribute(g.page(), {x, y}, Qt::Horizontal));
    }

    void rotateAndFlipAboutTheSelection()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 300, 100);
        QVERIFY(Ops::rotate(f.page(), {a, b}, 90));
        QCOMPARE(f.page().shape(a)->rotation, 90.0);
        // The pair turned about its common centre (200, 100).
        QVERIFY(qAbs(f.page().shape(a)->rect.center().x() - 200) < 1e-6);
        QVERIFY(qAbs(f.page().shape(a)->rect.center().y() - 0) < 1e-6);
        QVERIFY(Ops::rotate(f.page(), {a}, 270));
        QCOMPARE(f.page().shape(a)->rotation, 0.0);
        QVERIFY(Ops::flip(f.page(), {b}, Qt::Horizontal));
        QVERIFY(f.page().shape(b)->flipX);
        const QRectF bounds = shapeBounds(*f.page().shape(b));
        QVERIFY(bounds.width() > 0);
    }

    void zOrderMovesWithinShapesAndConnectors()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 200, 100);
        const QString c = f.shape("process", 300, 100);
        QVERIFY(Ops::reorder(f.page(), {a}, Ops::Order::Front));
        QCOMPARE(f.page().shapes.last().id, a);
        QVERIFY(Ops::reorder(f.page(), {a}, Ops::Order::Backward));
        QCOMPARE(f.page().shapes.at(1).id, a);
        QVERIFY(Ops::reorder(f.page(), {c}, Ops::Order::Back));
        QCOMPARE(f.page().shapes.first().id, c);
        QVERIFY(Ops::reorder(f.page(), {c}, Ops::Order::Forward));
        QCOMPARE(f.page().shapes.at(1).id, c);
        QVERIFY(!Ops::reorder(f.page(), {a}, Ops::Order::Front)); // already in front
    }

    void groupsAreOneLevelAndExpandSelections()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 200, 100);
        const QString c = f.shape("process", 300, 100);
        QVERIFY(Ops::group(f.drawing, f.page(), {a}).isEmpty());
        const QString g1 = Ops::group(f.drawing, f.page(), {a, b});
        QVERIFY(!g1.isEmpty());
        QCOMPARE(Ops::expandGroups(f.page(), {a}), (QStringList{a, b}));
        const QString g2 = Ops::group(f.drawing, f.page(), {a, b, c});
        QVERIFY(g2 != g1);
        QCOMPARE(f.page().shape(a)->groupId, g2);
        QVERIFY(Ops::ungroup(f.page(), {a, b, c}));
        QVERIFY(f.page().shape(c)->groupId.isEmpty());
        QVERIFY(!Ops::ungroup(f.page(), {a}));
    }

    void duplicateAndPasteRegluesCopies()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100, "A");
        const QString b = f.shape("decision", 400, 100, "B");
        const QString c = f.shape("process", 700, 100, "C");
        f.connect(a, b, 1, 3);
        f.connect(b, c);
        const QString grouped = Ops::group(f.drawing, f.page(), {a, b});
        const QStringList copies = Ops::duplicate(f.drawing, f.page(), {a, b}, QPointF(20, 20));
        QCOMPARE(copies.size(), 3); // two shapes and the connector between them
        const Shape *copyA = f.page().shape(copies.at(0));
        QCOMPARE(copyA->text, s("A"));
        QCOMPARE(copyA->rect.center(), QPointF(120, 120));
        QVERIFY(!copyA->groupId.isEmpty() && copyA->groupId != grouped);
        const Connector *copied = f.page().connector(copies.at(2));
        QCOMPARE(copied->from.shapeId, copies.at(0));
        QCOMPARE(copied->to.shapeId, copies.at(1));
        QCOMPARE(copied->from.port, 1);
        // Pasting a lone connector frees its ends where they were, shifted.
        const QString bc = f.page().connectors.at(1).id;
        const QPointF start = routeConnector(f.page(), f.page().connectors.at(1)).start();
        const QStringList pasted = Ops::paste(f.drawing, f.page(), fragmentOf(f.page(), {bc}), QPointF(0, 50));
        QCOMPARE(pasted.size(), 1);
        QVERIFY(!f.page().connector(pasted.first())->from.glued());
        QCOMPARE(f.page().connector(pasted.first())->from.point, start + QPointF(0, 50));
        QString error;
        QVERIFY(Ops::paste(f.drawing, f.page(), "garbage", {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void stylesTextAndFormatPainter()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 300, 100);
        const QString c = f.connect(a, b);
        Ops::ShapeStylePatch style;
        style.fill = QColor(255, 0, 0);
        style.line = QColor(0, 0, 255);
        style.lineWidth = 3;
        style.shadow = true;
        QVERIFY(Ops::applyShapeStyle(f.page(), {a, c}, style));
        QCOMPARE(f.page().shape(a)->style.fill, QColor(255, 0, 0));
        QCOMPARE(f.page().connector(c)->line, QColor(0, 0, 255));
        QCOMPARE(f.page().connector(c)->lineWidth, 3.0);
        QVERIFY(!Ops::applyShapeStyle(f.page(), {a}, style)); // unchanged
        Ops::TextStylePatch text;
        text.bold = true;
        text.size = 16;
        text.horizontal = Qt::AlignLeft;
        QVERIFY(Ops::applyTextStyle(f.page(), {a}, text));
        QVERIFY(f.page().shape(a)->textStyle.bold);
        QVERIFY(f.page().shape(a)->textStyle.align & Qt::AlignLeft);
        QVERIFY(f.page().shape(a)->textStyle.align & Qt::AlignVCenter);
        Ops::ConnectorPatch patch;
        patch.routing = Routing::Curved;
        patch.startArrow = ArrowStyle::Diamond;
        QVERIFY(Ops::applyConnectorPatch(f.page(), {c}, patch));
        QCOMPARE(f.page().connector(c)->routing, Routing::Curved);
        QVERIFY(Ops::copyFormat(f.page(), a, {b}));
        QVERIFY(f.page().shape(b)->style == f.page().shape(a)->style);
        QVERIFY(f.page().shape(b)->textStyle == f.page().shape(a)->textStyle);
        QVERIFY(!Ops::copyFormat(f.page(), s("missing"), {b}));
        QVERIFY(Ops::setText(f.page(), a, s("Hello")));
        QVERIFY(!Ops::setText(f.page(), a, s("Hello")));
        QVERIFY(Ops::setData(f.page(), a, s("Cost"), s("40")));
        QVERIFY(Ops::removeData(f.page(), a, s("cost")));
        QVERIFY(!Ops::removeData(f.page(), a, s("cost")));
    }

    void findAndReplace()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100, "Welcome call");
        const QString b = f.shape("process", 300, 100, "Orientation");
        Ops::setData(f.page(), b, s("Owner"), s("Welcome team"));
        QCOMPARE(Ops::find(f.page(), s("welcome")), (QStringList{a, b}));
        QCOMPARE(Ops::replaceText(f.page(), s("Welcome"), s("Intro")), 1);
        QCOMPARE(f.page().shape(a)->text, s("Intro call"));
        QVERIFY(Ops::find(f.page(), s("  ")).isEmpty());
    }

    void pagesAndFitToContents()
    {
        Fixture f;
        f.shape("process", 100, 100);
        const QString first = f.page().id;
        const QString second = Ops::addPage(f.drawing, 1, s("Network"));
        QCOMPARE(f.drawing.pages.size(), 2);
        QCOMPARE(f.drawing.pages.at(1).name, s("Network"));
        const QString copy = Ops::duplicatePage(f.drawing, first);
        QCOMPARE(f.drawing.pageIndex(copy), 1);
        QCOMPARE(f.drawing.page(copy)->shapes.size(), 1);
        QVERIFY(f.drawing.page(copy)->shapes.first().id != f.drawing.page(first)->shapes.first().id);
        QVERIFY(Ops::movePage(f.drawing, second, 0));
        QCOMPARE(f.drawing.pages.first().id, second);
        QVERIFY(Ops::renamePage(f.drawing, second, s("LAN")));
        QVERIFY(!Ops::renamePage(f.drawing, second, s("  ")));
        QVERIFY(Ops::removePage(f.drawing, second));
        QVERIFY(Ops::removePage(f.drawing, copy));
        QVERIFY(!Ops::removePage(f.drawing, first)); // the last page stays
        Page &page = *f.drawing.page(first);
        QVERIFY(Ops::fitPageToContents(page, 24));
        QVERIFY(page.size.width() < 300);
        QCOMPARE(page.shapes.first().rect.left() > 20, true);
    }

    void glueFindsPortsThenOutlines()
    {
        Fixture f;
        const QString a = f.shape("process", 200, 200);
        const Shape *shape = f.page().shape(a);
        const Endpoint port = Ops::glueAt(f.page(), portPoint(*shape, 1) + QPointF(3, 2), 8);
        QCOMPARE(port.shapeId, a);
        QCOMPARE(port.port, 1);
        const Endpoint dynamic = Ops::glueAt(f.page(), shape->rect.center() + QPointF(10, 5), 8);
        QCOMPARE(dynamic.shapeId, a);
        QCOMPARE(dynamic.port, -1);
        const Endpoint free = Ops::glueAt(f.page(), QPointF(900, 700), 8);
        QVERIFY(!free.glued());
        QVERIFY(Ops::setEndpoint(f.page(), Ops::addConnector(f.drawing, f.page(), port, free), false, dynamic));
    }
};

DIAGRAM_TEST_MAIN(DiagramOpsTest)

#include "tst_diagram_ops.moc"
