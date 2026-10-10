// SPDX-License-Identifier: GPL-3.0-or-later
// M87 (D-095) structure: Visio's containers (wrap the selection, expand
// around a member dragged half out, fit, disband, Delete taking members),
// cross-functional swimlanes (insert after/before, the pool closing up,
// resizing one lane resizing the pool, dragging a lane to a new place,
// phases spanning the pool), AutoConnect, the Re-Layout styles (mirrored
// flowcharts, compact tree, radial, circular) and right-angle routing
// that goes round shapes when no quick route is clear, with line jumps
// by style and direction.

#include "diagram_fixture.h"

#include "geometry.h"
#include "layout.h"
#include "routing.h"
#include "stencils.h"

#include <QLineF>

using namespace DiagramTest;

namespace {

QRectF rectOf(Fixture &f, const QString &id)
{
    return f.page().shape(id)->rect;
}

// Settles an edit the way the app does (DiagramService::editPage).
template<typename Change>
void edit(Fixture &f, Change change)
{
    const Page before = f.page();
    change(f.page());
    Ops::settle(f.page(), before);
}

bool crossesAShape(const Page &page, const Route &route, const QStringList &except)
{
    const QVector<QPointF> points = route.points;
    for (int i = 1; i + 2 < points.size(); ++i) {
        for (const Shape &s : page.shapes) {
            if (except.contains(s.id)) {
                continue;
            }
            const QRectF r = shapeBounds(s).adjusted(1, 1, -1, -1);
            const QLineF seg(points.at(i), points.at(i + 1));
            for (double t = 0; t <= 1.0; t += 0.02) {
                if (r.contains(seg.pointAt(t))) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

class DiagramM87StructureTest final : public QObject {
    Q_OBJECT
private slots:
    void containersBehaveAsVisio()
    {
        Fixture f;
        const QString a = f.shape("process", 200, 200, "A");
        const QString b = f.shape("process", 420, 260, "B");
        // Insert ▸ Container wraps the selection, behind it, with a margin
        // and its header band.
        const QString box = Ops::wrapInContainer(f.drawing, f.page(), {a, b});
        QVERIFY(!box.isEmpty());
        QCOMPARE(f.page().shapes.first().id, box);
        QCOMPARE(Ops::containerMembers(f.page(), box), (QStringList{a, b}));
        const QRectF wrapped = rectOf(f, box);
        QVERIFY(wrapped.contains(rectOf(f, a)) && wrapped.contains(rectOf(f, b)));
        QCOMPARE(rectOf(f, a).left() - wrapped.left(), Ops::containerMargin(*f.page().shape(box)));
        // Expand as needed: a member dragged half out grows the container.
        edit(f, [&](Page &p) { Ops::moveItems(p, {b}, QPointF(60, 0)); });
        QVERIFY(rectOf(f, box).contains(rectOf(f, b)));
        QVERIFY(rectOf(f, box).right() > wrapped.right());
        // A shape dragged right out leaves (its centre outside): no growth.
        const QRectF grown = rectOf(f, box);
        edit(f, [&](Page &p) { Ops::moveItems(p, {a}, QPointF(0, 600)); });
        QCOMPARE(rectOf(f, box), grown);
        QVERIFY(!Ops::containerMembers(f.page(), box).contains(a));
        // A shape dropped into it is taken in.
        QString c;
        edit(f, [&](Page &p) { c = Ops::addShape(f.drawing, p, QStringLiteral("decision"), rectOf(f, box).bottomRight() - QPointF(10, 10)); });
        QVERIFY(rectOf(f, box).contains(rectOf(f, c)));
        // Always fit: the container hugs its members.
        QVERIFY(Ops::setContainerResize(f.page(), {box}, ContainerResize::Fit));
        QVERIFY(Ops::fitToContents(f.page(), {box}));
        QRectF members = rectOf(f, b) | rectOf(f, c);
        const QRectF fitted = rectOf(f, box);
        QCOMPARE(fitted.right(), members.right() + Ops::containerMargin(*f.page().shape(box)));
        // A fixed container never resizes.
        QVERIFY(Ops::setContainerResize(f.page(), {box}, ContainerResize::None));
        edit(f, [&](Page &p) { Ops::moveItems(p, {c}, QPointF(30, 0)); });
        QCOMPARE(rectOf(f, box), fitted);
        edit(f, [&](Page &p) { Ops::moveItems(p, {c}, QPointF(-30, 0)); });
        // Delete takes the members (and the connector between them) along;
        // Disband keeps them.
        Ops::addConnector(f.drawing, f.page(), {b, -1, {}}, {c, -1, {}});
        Fixture g = f;
        QCOMPARE(Ops::deleteItems(g.page(), {box}), 4);
        QCOMPARE(g.page().shapes.size(), 1); // only A, outside
        QVERIFY(g.page().connectors.isEmpty());
        QVERIFY(Ops::disband(f.page(), {box}));
        QCOMPARE(f.page().shapes.size(), 3);
        QCOMPARE(f.page().connectors.size(), 1);
    }

    void poolsStayStackedAndEquallyLong()
    {
        Fixture f;
        const QString l1 = f.shape("laneHorizontal", 400, 175, "Sales");
        Shape *first = f.page().shape(l1);
        first->rect = QRectF(40, 100, 720, 150);
        const QString step = f.shape("process", 300, 175, "Quote");
        // Insert after, then before: each lane takes the reference's size;
        // what follows makes room with its contents.
        const QString l2 = Ops::insertLane(f.drawing, f.page(), l1, true, {}, {});
        QCOMPARE(rectOf(f, l2), QRectF(40, 250, 720, 150));
        QCOMPARE(Ops::poolOf(f.page(), l1), (QStringList{l1, l2}));
        QString l0; // settled as the app settles it: the pool is already whole
        edit(f, [&](Page &p) { l0 = Ops::insertLane(f.drawing, p, l1, false, {}, {}); });
        QCOMPARE(rectOf(f, l0), QRectF(40, 100, 720, 150));
        QCOMPARE(rectOf(f, l1), QRectF(40, 250, 720, 150));
        QCOMPARE(rectOf(f, step).center(), QPointF(300, 325)); // moved with its lane
        QCOMPARE(Ops::poolOf(f.page(), l2), (QStringList{l0, l1, l2}));
        // No lane chosen: the new lane joins the nearest pool, at its end.
        const QString l3 = Ops::insertLane(f.drawing, f.page(), {}, true, QStringLiteral("laneHorizontal"), QPointF(400, 700));
        QCOMPARE(rectOf(f, l3).top(), 550.0);
        // A phase spans the whole pool, clear of the lane headers.
        QString phase; // settled as the app settles it: no lane takes the phase in
        edit(f, [&](Page &p) { phase = Ops::insertPhase(f.drawing, p, QPointF(500, 300)); });
        QCOMPARE(rectOf(f, phase).top(), 100.0);
        QCOMPARE(rectOf(f, phase).height(), 600.0);
        // Resizing one lane's depth pushes the lanes after it; its length
        // becomes the pool's.
        edit(f, [&](Page &p) { Ops::setShapeGeometry(p, l1, QRectF(40, 250, 800, 200), 0); });
        QCOMPARE(rectOf(f, l2), QRectF(40, 450, 800, 150));
        QCOMPARE(rectOf(f, l3), QRectF(40, 600, 800, 150));
        QCOMPARE(rectOf(f, l0).width(), 800.0);
        QCOMPARE(rectOf(f, phase).height(), 650.0); // still across the pool
        // Dragging a lane along the stack moves it to that place, contents along.
        edit(f, [&](Page &p) { Ops::moveItems(p, {l1}, QPointF(0, 260)); });
        QCOMPARE(Ops::poolOf(f.page(), l0), (QStringList{l0, l2, l1, l3}));
        QCOMPARE(rectOf(f, l1).top(), 400.0);
        QVERIFY(rectOf(f, l1).contains(rectOf(f, step)));
        // Deleting a lane closes the pool and takes the lane's shapes.
        QVERIFY(Ops::deleteItems(f.page(), {l1}) >= 2);
        QVERIFY(f.page().shape(step) == nullptr);
        QCOMPARE(rectOf(f, l3).top(), 400.0);
        QCOMPARE(Ops::poolOf(f.page(), l0), (QStringList{l0, l2, l3}));
        // A member growing past its lane's edge grows the lane and the pool.
        const QString wide = f.shape("process", 300, 175, "Wide");
        edit(f, [&](Page &p) { Ops::setShapeGeometry(p, wide, QRectF(240, 150, 132, 160), 0); });
        QVERIFY(rectOf(f, l0).contains(rectOf(f, wide)));
        QCOMPARE(rectOf(f, l2).top(), rectOf(f, l0).bottom());
    }

    void autoConnectAddsOrJoins()
    {
        Fixture f;
        const QString a = f.shape("process", 200, 200, "A");
        QString c;
        const QString b = Ops::autoConnect(f.drawing, f.page(), a, 1, {}, 48, &c);
        QVERIFY(!b.isEmpty() && !c.isEmpty());
        QCOMPARE(f.page().shape(b)->type, QStringLiteral("process"));
        QVERIFY(f.page().shape(b)->text.isEmpty());
        QCOMPARE(rectOf(f, b).left() - rectOf(f, a).right(), 48.0);
        QCOMPARE(rectOf(f, b).center().y(), rectOf(f, a).center().y());
        const Connector *link = f.page().connector(c);
        QCOMPARE(link->from.port, 1);
        QCOMPARE(link->to.port, 3);
        // Below, with a quick shape: a decision.
        const QString d = Ops::autoConnect(f.drawing, f.page(), a, 2, QStringLiteral("decision"));
        QCOMPARE(f.page().shape(d)->type, QStringLiteral("decision"));
        QVERIFY(rectOf(f, d).top() > rectOf(f, a).bottom());
        // Towards a shape in line it connects instead of adding one.
        const QString e = Ops::autoConnect(f.drawing, f.page(), d, 1, {}); // nothing in line: added
        const int shapes = int(f.page().shapes.size());
        const int connectors = int(f.page().connectors.size());
        QCOMPARE(Ops::autoConnect(f.drawing, f.page(), b, 2, {}), e); // e lies under b
        QCOMPARE(int(f.page().shapes.size()), shapes);
        QCOMPARE(int(f.page().connectors.size()), connectors + 1);
        // A new shape steps past a shape in its way that is not in line.
        const QString label = f.shape("text", 600, 500, "Label");
        const QString blocker = f.shape("rectangle", 800, 565);
        f.page().shape(blocker)->rect = QRectF(700, 525, 200, 80);
        const QString placed = Ops::autoConnect(f.drawing, f.page(), label, 1, QStringLiteral("decision"));
        QVERIFY(placed != blocker);
        QVERIFY(rectOf(f, placed).left() > rectOf(f, blocker).right());
        QVERIFY(!rectOf(f, placed).intersects(rectOf(f, blocker)));
    }

    void relayoutStyles()
    {
        // A small tree: root → a, b; a → c, d.
        const auto build = [](Fixture &f) {
            QStringList ids;
            for (const char *t : {"R", "A", "B", "C", "D"}) {
                ids.append(f.shape("process", 100 + ids.size() * 150, 100 + ids.size() * 37, t));
            }
            f.connect(ids[0], ids[1]);
            f.connect(ids[0], ids[2]);
            f.connect(ids[1], ids[3]);
            f.connect(ids[1], ids[4]);
            return ids;
        };
        {
            Fixture f;
            const QStringList ids = build(f);
            QVERIFY(autoLayout(f.page(), {}, {LayoutKind::BottomUp}));
            QVERIFY(rectOf(f, ids[0]).center().y() > rectOf(f, ids[1]).center().y()); // root at the bottom
            QVERIFY(rectOf(f, ids[1]).center().y() > rectOf(f, ids[3]).center().y());
            QCOMPARE(f.page().connectors.first().from.port, 0); // leaves the root's top
        }
        {
            Fixture f;
            const QStringList ids = build(f);
            QVERIFY(autoLayout(f.page(), {}, {LayoutKind::RightLeft}));
            QVERIFY(rectOf(f, ids[0]).center().x() > rectOf(f, ids[1]).center().x());
        }
        {
            Fixture f;
            const QStringList ids = build(f);
            QVERIFY(autoLayout(f.page(), {}, {LayoutKind::CompactTree, 40, 20}));
            const QRectF r = rectOf(f, ids[0]);
            QVERIFY(rectOf(f, ids[1]).left() > r.left() && rectOf(f, ids[1]).top() > r.bottom());
            QVERIFY(rectOf(f, ids[3]).left() > rectOf(f, ids[1]).left()); // indented again
            QVERIFY(rectOf(f, ids[2]).top() > rectOf(f, ids[4]).bottom()); // B after A's subtree
            QCOMPARE(f.page().connectors.first().from.port, 2);
            QCOMPARE(f.page().connectors.first().to.port, 3);
        }
        {
            Fixture f;
            const QStringList ids = build(f);
            QVERIFY(autoLayout(f.page(), {}, {LayoutKind::Radial, 56, 36}));
            const QPointF centre = rectOf(f, ids[0]).center();
            const double ra = QLineF(centre, rectOf(f, ids[1]).center()).length();
            const double rb = QLineF(centre, rectOf(f, ids[2]).center()).length();
            const double rc = QLineF(centre, rectOf(f, ids[3]).center()).length();
            QVERIFY(qAbs(ra - rb) < 0.5);         // one ring per level
            QVERIFY(rc > ra * 1.5);               // the next level further out
            for (const Connector &c : f.page().connectors) {
                QCOMPARE(c.routing, Routing::Straight);
                QCOMPARE(c.from.port, -1);
            }
        }
        {
            Fixture f;
            const QStringList ids = build(f);
            QVERIFY(autoLayout(f.page(), {}, {LayoutKind::Circular, 56, 36}));
            QRectF all;
            for (const QString &id : ids) {
                all = all.isNull() ? rectOf(f, id) : all | rectOf(f, id);
            }
            QPointF centre;
            for (const QString &id : ids) {
                centre += rectOf(f, id).center() / ids.size();
            }
            const double r0 = QLineF(centre, rectOf(f, ids[0]).center()).length();
            for (const QString &id : ids) {
                QVERIFY(qAbs(QLineF(centre, rectOf(f, id).center()).length() - r0) < 25);
                for (const QString &other : ids) {
                    QVERIFY(id == other || !rectOf(f, id).intersects(rectOf(f, other)));
                }
            }
            QCOMPARE(layoutKindOf(layoutKindName(LayoutKind::Circular)), LayoutKind::Circular);
        }
    }

    void rightAngleRoutesGoRoundShapes()
    {
        // A wall between the two ends that every L/Z/U candidate crosses.
        Fixture f;
        const QString a = f.shape("process", 100, 300, "A");
        const QString b = f.shape("process", 700, 300, "B");
        const QString top = f.shape("rectangle", 400, 140);
        f.page().shape(top)->rect = QRectF(300, 40, 200, 220);
        const QString low = f.shape("rectangle", 400, 460);
        f.page().shape(low)->rect = QRectF(300, 340, 200, 220);
        const QString mid = f.shape("rectangle", 400, 300);
        f.page().shape(mid)->rect = QRectF(360, 240, 80, 120);
        const QString lid = f.shape("rectangle", 400, 20);
        f.page().shape(lid)->rect = QRectF(150, -40, 500, 70);
        const QString c = f.connect(a, b, 1, 3);
        const Route route = routeConnector(f.page(), *f.page().connector(c));
        QVERIFY2(!crossesAShape(f.page(), route, {a, b}), "the route crosses a shape although a way round exists");
        for (int i = 0; i + 1 < route.points.size(); ++i) {
            const QPointF d = route.points.at(i + 1) - route.points.at(i);
            QVERIFY(qAbs(d.x()) < 1e-6 || qAbs(d.y()) < 1e-6); // Manhattan
        }
    }

    void lineJumpsByStyleAndDirection()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 300);
        const QString b = f.shape("process", 600, 300);
        const QString c = f.shape("process", 350, 80);
        const QString d = f.shape("process", 350, 560);
        const QString across = f.connect(a, b, 1, 3); // horizontal, first
        const QString down = f.connect(c, d, 2, 0);   // vertical, later
        f.page().lineJumps = true;
        // Last routed (Visio's default here): the later, vertical line hops.
        QHash<QString, Route> routes = routePage(f.page());
        QVERIFY(routes.value(across).jumps.isEmpty());
        QCOMPARE(routes.value(down).jumps.size(), 1);
        // On horizontal lines: the horizontal one hops instead.
        f.page().jumpOn = JumpOn::Horizontal;
        routes = routePage(f.page());
        QCOMPARE(routes.value(across).jumps.size(), 1);
        QVERIFY(routes.value(down).jumps.isEmpty());
        // Arc, gap and square draw three different paths: a curve over the
        // crossing, a break in the line, a square bump.
        QStringList shapes;
        for (const JumpStyle style : {JumpStyle::Arc, JumpStyle::Gap, JumpStyle::Square}) {
            f.page().jumpStyle = style;
            const Route r = routePage(f.page()).value(across);
            QCOMPARE(r.jumpStyle, style);
            const QPainterPath path = routePath(r);
            QString kinds;
            for (int i = 0; i < path.elementCount(); ++i) {
                kinds += QString::number(int(path.elementAt(i).type));
            }
            shapes.append(kinds);
        }
        QCOMPARE(QSet<QString>(shapes.cbegin(), shapes.cend()).size(), 3);
        QVERIFY(shapes.at(0).contains(QLatin1Char('2')));          // a curve
        QCOMPARE(shapes.at(1).count(QLatin1Char('0')), 2);          // two moves: the gap
        QVERIFY(!shapes.at(2).contains(QLatin1Char('2')));         // straight lines only
    }
};

QTEST_MAIN(DiagramM87StructureTest)
#include "tst_diagram_m87_structure.moc"
