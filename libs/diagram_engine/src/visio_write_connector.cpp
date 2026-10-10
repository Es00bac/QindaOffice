// SPDX-License-Identifier: GPL-3.0-or-later
// One written connector (a 1-D shape glued by formulas and <Connects>) and
// the frame of a written group (visio_write_p.h).
#include "visio_write_cells.h"

#include "text_paint.h"
#include "visio_geometry.h"
#include "visio_write_m87.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

QString n(double v)
{
    return numberText(v);
}

// AGENT-CONTRACT (Visio's glue, as Visio itself writes it — the formulas
// keep an end on its shape when the shape moves in Visio, the <Connect>
// rows of the page list the same glue): a connection point is
// PAR(PNT(Sheet.N!Connections.Xk,Sheet.N!Connections.Yk)) with k = port+1;
// the outline ("dynamic glue") is _WALKGLUE over the end's trigger, which
// _XFTRIGGER(Sheet.N!EventXFMod) fires when shape N moves.
void writeEnd(XmlElement &e, bool begin, QPointF at, GlueEnd glue)
{
    const QString x = begin ? QStringLiteral("BeginX") : QStringLiteral("EndX");
    const QString y = begin ? QStringLiteral("BeginY") : QStringLiteral("EndY");
    const QString trigger = begin ? QStringLiteral("BegTrigger") : QStringLiteral("EndTrigger");
    QString formula;
    if (glue.sheet > 0 && glue.port >= 0) {
        formula = QStringLiteral("PAR(PNT(Sheet.%1!Connections.X%2,Sheet.%1!Connections.Y%2))")
                      .arg(glue.sheet)
                      .arg(glue.port + 1);
    } else if (glue.sheet > 0) {
        formula = begin ? QStringLiteral("_WALKGLUE(BegTrigger,EndTrigger,WalkPreference)")
                        : QStringLiteral("_WALKGLUE(EndTrigger,BegTrigger,WalkPreference)");
    }
    setCell(e, x, n(at.x()), formula);
    setCell(e, y, n(at.y()), formula);
    if (glue.sheet > 0) {
        setCell(e, trigger, QStringLiteral("2"), QStringLiteral("_XFTRIGGER(Sheet.%1!EventXFMod)").arg(glue.sheet));
    } else {
        setCell(e, trigger, QStringLiteral("0"), QStringLiteral("No Formula"));
    }
}

// The 1-D cells and the route's geometry. The local frame is the one
// Visio's dynamic connector uses: Angle 0, Width = EndX-BeginX and Height =
// EndY-BeginY (signed), a quarter inch across where the route is flat.
void writeRoute(XmlElement &e, const QVector<QPointF> &route, bool curved, const QTransform &toParent,
                GlueEnd begin, GlueEnd end, const XmlElement *master)
{
    QVector<QPointF> points;
    for (const QPointF &p : route) {
        points.append(toParent.map(p));
    }
    if (points.size() < 2) {
        return;
    }
    const QPointF b = points.first();
    const QPointF f = points.last();
    const bool flatX = std::abs(f.x() - b.x()) < 1e-6;
    const bool flatY = std::abs(f.y() - b.y()) < 1e-6;
    const double w = flatX ? 0.25 : f.x() - b.x();
    const double h = flatY ? 0.25 : f.y() - b.y();
    const QPointF origin = b - QPointF(flatX ? 0.125 : 0, flatY ? 0.125 : 0);
    setCell(e, QStringLiteral("PinX"), n(origin.x() + w / 2), QStringLiteral("GUARD((BeginX+EndX)/2)"));
    setCell(e, QStringLiteral("PinY"), n(origin.y() + h / 2), QStringLiteral("GUARD((BeginY+EndY)/2)"));
    setCell(e, QStringLiteral("Width"), n(w), flatX ? QStringLiteral("GUARD(0.25DL)") : QStringLiteral("GUARD(EndX-BeginX)"));
    setCell(e, QStringLiteral("Height"), n(h), flatY ? QStringLiteral("GUARD(0.25DL)") : QStringLiteral("GUARD(EndY-BeginY)"));
    setCell(e, QStringLiteral("LocPinX"), n(w / 2), QStringLiteral("Width*0.5"));
    setCell(e, QStringLiteral("LocPinY"), n(h / 2), QStringLiteral("Height*0.5"));
    setCell(e, QStringLiteral("Angle"), QStringLiteral("0"));
    setCell(e, QStringLiteral("FlipX"), QStringLiteral("0"));
    setCell(e, QStringLiteral("FlipY"), QStringLiteral("0"));
    writeEnd(e, true, b, begin);
    writeEnd(e, false, f, end);
    QVector<QPointF> local;
    for (const QPointF &p : std::as_const(points)) {
        local.append(p - origin);
    }
    // AGENT-NOTE: the route goes into Geometry section 0, row for row over
    // the master's, with the master's surplus rows deleted — what Visio
    // writes for a re-routed connector, and the section its router redraws
    // when a glued shape moves in Visio (a hidden section 0 plus a new one
    // would leave the new one behind as a stale line).
    removeSections(e, u"Geometry");
    XmlElementPtr section = routeSection(local, curved, w, h, 0);
    const int rows = int(section->elements(kNs, u"Row").size());
    if (master != nullptr) {
        for (const XmlElement *ms : children(*master, u"Section")) {
            if (ms->attribute({}, u"N") != QLatin1String("Geometry")) {
                continue;
            }
            const int ix = intAttribute(*ms, u"IX", 0);
            if (ix != 0) {
                XmlElementPtr hidden = makeElement(QStringLiteral("Section"));
                hidden->setAttribute({}, QStringLiteral("N"), QStringLiteral("Geometry"));
                hidden->setAttribute({}, QStringLiteral("IX"), QString::number(ix));
                appendCell(*hidden, QStringLiteral("NoShow"), QStringLiteral("1"));
                insertChild(e, hidden);
                continue;
            }
            for (const XmlElement *row : children(*ms, u"Row")) {
                const int rowIx = intAttribute(*row, u"IX", 0);
                if (rowIx > rows) {
                    XmlElement &del = appendElement(*section, QStringLiteral("Row"));
                    del.setAttribute({}, QStringLiteral("T"), row->attribute({}, u"T"));
                    del.setAttribute({}, QStringLiteral("IX"), QString::number(rowIx));
                    del.setAttribute({}, QStringLiteral("Del"), QStringLiteral("1"));
                }
            }
        }
    }
    insertChild(e, section);
}

void writeLabel(WriteContext &ctx, XmlElement &e, const Connector &c, const QVector<QPointF> &route,
                const QTransform &toParent)
{
    if (c.text.isEmpty()) {
        return;
    }
    // The label's centre along the route, in the connector's local frame
    // (its origin is where BeginX/BeginY minus the flat offsets put it).
    double length = 0;
    for (qsizetype i = 1; i < route.size(); ++i) {
        length += QLineF(route.at(i - 1), route.at(i)).length();
    }
    QPointF at = route.isEmpty() ? QPointF() : route.first();
    double walked = 0;
    const double wanted = length * std::clamp(c.labelPosition, 0.0, 1.0);
    for (qsizetype i = 1; i < route.size(); ++i) {
        const QLineF segment(route.at(i - 1), route.at(i));
        if (walked + segment.length() >= wanted && segment.length() > 0) {
            at = segment.pointAt((wanted - walked) / segment.length());
            break;
        }
        walked += segment.length();
    }
    const QPointF b = toParent.map(route.first());
    const QPointF f = toParent.map(route.last());
    const QPointF origin = b - QPointF(std::abs(f.x() - b.x()) < 1e-6 ? 0.125 : 0, std::abs(f.y() - b.y()) < 1e-6 ? 0.125 : 0);
    const QPointF local = toParent.map(at) - origin;
    const double k = pxPerInch(toParent);
    const QSizeF size = TextPaint::measure(c.text, c.textStyle) + QSizeF(12, 6);
    setCell(e, QStringLiteral("TxtPinX"), n(local.x()));
    setCell(e, QStringLiteral("TxtPinY"), n(local.y()));
    setCell(e, QStringLiteral("TxtWidth"), n(size.width() / k));
    setCell(e, QStringLiteral("TxtHeight"), n(size.height() / k));
    setCell(e, QStringLiteral("TxtLocPinX"), n(size.width() / k / 2), QStringLiteral("TxtWidth*0.5"));
    setCell(e, QStringLiteral("TxtLocPinY"), n(size.height() / k / 2), QStringLiteral("TxtHeight*0.5"));
    setCell(e, QStringLiteral("TxtAngle"), QStringLiteral("0"));
    TextStyle style = c.textStyle;
    style.align = Qt::AlignCenter;
    setCell(e, QStringLiteral("VerticalAlign"), QStringLiteral("1"));
    writeText(e, c.text, style, c.textStyle.family.isEmpty() ? ctx.defaultFont : c.textStyle.family);
}

void writeArrows(XmlElement &e, const Connector &c)
{
    setCell(e, QStringLiteral("BeginArrow"), QString::number(visioArrow(c.startArrow)));
    setCell(e, QStringLiteral("EndArrow"), QString::number(visioArrow(c.endArrow)));
    setCell(e, QStringLiteral("BeginArrowSize"), QStringLiteral("2"));
    setCell(e, QStringLiteral("EndArrowSize"), QStringLiteral("2"));
}

void writeRouteStyle(XmlElement &e, Routing routing)
{
    // ShapeRouteStyle 1 = right angle, 2 = straight; ConLineRouteExt 2 =
    // curved lines.
    setCell(e, QStringLiteral("ShapeRouteStyle"), routing == Routing::Straight ? QStringLiteral("2") : QStringLiteral("1"));
    setCell(e, QStringLiteral("ConLineRouteExt"), routing == Routing::Curved ? QStringLiteral("2") : QStringLiteral("1"));
}

} // namespace

XmlElementPtr freshConnector(WriteContext &ctx, const Connector &connector, int id, const QVector<QPointF> &route,
                             bool curved, const QTransform &toParent, GlueEnd begin, GlueEnd end)
{
    XmlElementPtr e = makeElement(QStringLiteral("Shape"));
    e->setAttribute({}, QStringLiteral("ID"), QString::number(id));
    setShapeNames(*e, QStringLiteral("Dynamic connector"), id);
    e->setAttribute({}, QStringLiteral("Type"), QStringLiteral("Shape"));
    for (const char *style : {"LineStyle", "FillStyle", "TextStyle"}) {
        e->setAttribute({}, QString::fromLatin1(style), QString::number(ctx.connectorStyle));
    }
    writeRoute(*e, route, curved, toParent, begin, end, nullptr);
    writeLine(*e, connector.line, connector.lineWidth, connector.pattern);
    writeArrows(*e, connector);
    ShapeStyle none;
    none.fill = QColor(Qt::transparent);
    writeFill(*e, none);
    setCell(*e, QStringLiteral("ObjType"), QStringLiteral("2"));
    writeRouteStyle(*e, connector.routing);
    writeLabel(ctx, *e, connector, route, toParent);
    if (!connector.data.isEmpty()) {
        insertChild(*e, dataSection(connector.data));
    }
    if (!connector.layers.isEmpty()) {
        writeLayerMember(*e, connector.layers); // M87
    }
    if (!connector.links.isEmpty()) {
        writeHyperlinks(*e, connector.links);
    }
    return e;
}

void updateConnector(WriteContext &ctx, XmlElement &element, const Connector &connector, const Connector &snapshot,
                     const QVector<QPointF> &route, bool curved, const QTransform &toParent, GlueEnd begin,
                     GlueEnd end, bool routeChanged, const XmlElement *master)
{
    const Sheet masterSheet = master != nullptr ? Sheet::merge(*master, nullptr) : Sheet();
    const Sheet merged = Sheet::merge(element, master != nullptr ? &masterSheet : nullptr);
    if (routeChanged) {
        writeRoute(element, route, curved, toParent, begin, end, master);
    }
    if (connector.line != snapshot.line || connector.lineWidth != snapshot.lineWidth
        || connector.pattern != snapshot.pattern) {
        writeLine(element, connector.line, connector.lineWidth, connector.pattern);
    }
    if (connector.startArrow != snapshot.startArrow || connector.endArrow != snapshot.endArrow) {
        writeArrows(element, connector);
    }
    if (connector.routing != snapshot.routing) {
        writeRouteStyle(element, connector.routing);
    }
    if (connector.text != snapshot.text || connector.textStyle != snapshot.textStyle
        || connector.labelPosition != snapshot.labelPosition || (routeChanged && !connector.text.isEmpty())) {
        if (connector.text.isEmpty()) {
            writeText(element, QString(), connector.textStyle, ctx.defaultFont);
        } else {
            writeLabel(ctx, element, connector, route, toParent);
        }
    }
    if (connector.data != snapshot.data) {
        updateData(element, snapshot.data, connector.data, merged, master != nullptr ? &masterSheet : nullptr);
    }
    if (connector.layers != snapshot.layers) {
        writeLayerMember(element, connector.layers); // M87
    }
    if (connector.links != snapshot.links) {
        writeHyperlinks(element, connector.links, master != nullptr ? &masterSheet : nullptr);
    }
}

void setGroupFrame(XmlElement &group, const QRectF &bounds, const QTransform &toParent)
{
    const double k = pxPerInch(toParent);
    const QPointF centre = toParent.map(bounds.center());
    const double w = std::max(1.0, bounds.width()) / k;
    const double h = std::max(1.0, bounds.height()) / k;
    setCell(group, QStringLiteral("PinX"), n(centre.x()));
    setCell(group, QStringLiteral("PinY"), n(centre.y()));
    setCell(group, QStringLiteral("Width"), n(w));
    setCell(group, QStringLiteral("Height"), n(h));
    setCell(group, QStringLiteral("LocPinX"), n(w / 2), QStringLiteral("Width*0.5"));
    setCell(group, QStringLiteral("LocPinY"), n(h / 2), QStringLiteral("Height*0.5"));
    setCell(group, QStringLiteral("Angle"), QStringLiteral("0"));
    setCell(group, QStringLiteral("FlipX"), QStringLiteral("0"));
    setCell(group, QStringLiteral("FlipY"), QStringLiteral("0"));
}

} // namespace QindaOffice::Diagram::Visio
