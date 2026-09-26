// SPDX-License-Identifier: GPL-3.0-or-later
// draw.io / diagrams.net import (importers.h): uncompressed mxGraphModel
// XML to pages. Stencil mapping by style keyword; geometry of children is
// relative to their parent cell (groups, swimlanes), resolved here.
#include "importers.h"

#include "geometry.h"
#include "painter.h"
#include "stencils.h"
#include "themes.h"

#include <QCoreApplication>
#include <QHash>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DrawioImport", text);
}

struct Cell {
    QString id;
    QString parent;
    QString value;
    QString style;
    bool vertex = false;
    bool edge = false;
    QString source;
    QString target;
    QRectF geometry;
    bool relative = false;
    QPointF sourcePoint;
    QPointF targetPoint;
    QList<DataField> data;
};

using Style = QHash<QString, QString>;

Style parseStyle(const QString &text, QString *shapeName)
{
    Style style;
    for (const QString &part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) {
            if (shapeName != nullptr && shapeName->isEmpty()) {
                *shapeName = part.trimmed();
            }
            style.insert(part.trimmed(), QStringLiteral("1"));
        } else {
            style.insert(part.left(eq).trimmed(), part.mid(eq + 1).trimmed());
        }
    }
    return style;
}

QColor colorFrom(const Style &style, const char *key, const QColor &fallback)
{
    const QString value = style.value(QLatin1String(key));
    if (value.isEmpty() || value == QLatin1String("default")) {
        return fallback;
    }
    if (value == QLatin1String("none")) {
        return QColor(Qt::transparent);
    }
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

double numberFrom(const Style &style, const char *key, double fallback)
{
    bool ok = false;
    const double value = style.value(QLatin1String(key)).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

QString stencilFor(const Style &style, const QString &name)
{
    const QString shape = style.value(QStringLiteral("shape"), name).toLower();
    const auto has = [&](const char *key) { return style.contains(QLatin1String(key)) || shape == QLatin1String(key); };
    static const QHash<QString, QString> flowchart = {
        {QStringLiteral("decision"), QStringLiteral("decision")},
        {QStringLiteral("terminator"), QStringLiteral("terminator")},
        {QStringLiteral("document"), QStringLiteral("document")},
        {QStringLiteral("data"), QStringLiteral("data")},
        {QStringLiteral("manual_input"), QStringLiteral("manualInput")},
        {QStringLiteral("predefined_process"), QStringLiteral("predefinedProcess")},
        {QStringLiteral("on-page_reference"), QStringLiteral("onPage")},
        {QStringLiteral("off-page_reference"), QStringLiteral("offPage")},
        {QStringLiteral("database"), QStringLiteral("database")},
        {QStringLiteral("preparation"), QStringLiteral("preparation")},
        {QStringLiteral("process"), QStringLiteral("process")},
    };
    if (shape.startsWith(QLatin1String("mxgraph.flowchart."))) {
        return flowchart.value(shape.mid(18), QStringLiteral("process"));
    }
    if (has("swimlane")) {
        return style.value(QStringLiteral("horizontal")) == QLatin1String("0") ? QStringLiteral("laneHorizontal")
                                                                              : QStringLiteral("container");
    }
    if (has("text") && !style.contains(QStringLiteral("shape"))) return QStringLiteral("text");
    if (has("rhombus")) return QStringLiteral("decision");
    if (has("triangle")) return QStringLiteral("triangle");
    if (shape == QLatin1String("hexagon")) return QStringLiteral("hexagon");
    if (shape == QLatin1String("parallelogram")) return QStringLiteral("data");
    if (shape.startsWith(QLatin1String("cylinder")) || shape == QLatin1String("datastore")) return QStringLiteral("database");
    if (shape == QLatin1String("cloud")) return QStringLiteral("cloud");
    if (shape == QLatin1String("document")) return QStringLiteral("document");
    if (shape == QLatin1String("process")) return QStringLiteral("predefinedProcess");
    if (shape == QLatin1String("umlactor")) return QStringLiteral("umlActor");
    if (shape == QLatin1String("note")) return QStringLiteral("note");
    if (shape == QLatin1String("step")) return QStringLiteral("chevron");
    if (shape == QLatin1String("singlearrow")) return QStringLiteral("arrowRight");
    if (shape == QLatin1String("doublearrow")) return QStringLiteral("arrowDouble");
    if (shape == QLatin1String("callout")) return QStringLiteral("calloutRect");
    if (shape == QLatin1String("image")) return QStringLiteral("picture");
    if (has("ellipse")) return QStringLiteral("ellipse");
    if (style.value(QStringLiteral("rounded")) == QLatin1String("1")) {
        return numberFrom(style, "arcSize", 20) >= 40 ? QStringLiteral("terminator") : QStringLiteral("roundedRectangle");
    }
    return QStringLiteral("rectangle");
}

ArrowStyle arrowFrom(const QString &value, ArrowStyle fallback)
{
    if (value.isEmpty()) return fallback;
    if (value == QLatin1String("none")) return ArrowStyle::None;
    if (value == QLatin1String("open") || value == QLatin1String("openThin")) return ArrowStyle::Open;
    if (value.startsWith(QLatin1String("diamond"))) return ArrowStyle::Diamond;
    if (value == QLatin1String("oval") || value == QLatin1String("circle") || value == QLatin1String("dot")) return ArrowStyle::Circle;
    return ArrowStyle::Filled;
}

QString plainText(const QString &value, const Style &style)
{
    if (style.value(QStringLiteral("html")) != QLatin1String("1")) {
        return value;
    }
    return QTextDocumentFragment::fromHtml(value).toPlainText().trimmed();
}

void applyTextStyle(TextStyle &text, const Style &style)
{
    text.color = colorFrom(style, "fontColor", text.color);
    if (style.contains(QStringLiteral("fontSize"))) {
        text.size = std::clamp(numberFrom(style, "fontSize", 12) * 0.75, 4.0, 200.0); // px -> pt
    }
    const int font = int(numberFrom(style, "fontStyle", 0));
    text.bold = font & 1;
    text.italic = font & 2;
    text.underline = font & 4;
    if (style.contains(QStringLiteral("fontFamily"))) {
        text.family = style.value(QStringLiteral("fontFamily"));
    }
    const QString align = style.value(QStringLiteral("align"));
    const QString vertical = style.value(QStringLiteral("verticalAlign"));
    Qt::Alignment a = text.align;
    if (!align.isEmpty()) {
        a = (a & ~Qt::AlignHorizontal_Mask)
            | (align == QLatin1String("left") ? Qt::AlignLeft : align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter);
    }
    if (!vertical.isEmpty()) {
        a = (a & ~Qt::AlignVertical_Mask)
            | (vertical == QLatin1String("top") ? Qt::AlignTop : vertical == QLatin1String("bottom") ? Qt::AlignBottom : Qt::AlignVCenter);
    }
    text.align = a;
}

LinePattern patternFrom(const Style &style)
{
    if (style.value(QStringLiteral("dashed")) != QLatin1String("1")) {
        return LinePattern::Solid;
    }
    const QString dash = style.value(QStringLiteral("dashPattern"));
    return dash.startsWith(QLatin1String("1 ")) ? LinePattern::Dot : LinePattern::Dash;
}

// Reads one <mxGraphModel> (the reader is on its start element).
bool readModel(QXmlStreamReader &xml, QVector<Cell> *cells, QSizeF *pageSize, QColor *background)
{
    const auto attrs = xml.attributes();
    const double width = attrs.value(QLatin1String("pageWidth")).toDouble();
    const double height = attrs.value(QLatin1String("pageHeight")).toDouble();
    if (width > 0 && height > 0) {
        *pageSize = QSizeF(width, height);
    }
    const QColor bg(attrs.value(QLatin1String("background")).toString());
    if (bg.isValid()) {
        *background = bg;
    }
    Cell *current = nullptr;
    QString wrapperLabel;
    QList<DataField> wrapperData;
    QString wrapperId;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("mxGraphModel")) {
            return true;
        }
        if (!xml.isStartElement()) {
            if (xml.isEndElement() && (xml.name() == QLatin1String("UserObject") || xml.name() == QLatin1String("object"))) {
                wrapperId.clear();
                wrapperData.clear();
                wrapperLabel.clear();
            }
            continue;
        }
        const auto a = xml.attributes();
        if (xml.name() == QLatin1String("UserObject") || xml.name() == QLatin1String("object")) {
            wrapperId = a.value(QLatin1String("id")).toString();
            wrapperLabel = a.value(QLatin1String("label")).toString();
            wrapperData.clear();
            for (const auto &attr : a) {
                const QString key = attr.name().toString();
                if (key != QLatin1String("id") && key != QLatin1String("label") && key != QLatin1String("placeholders")) {
                    wrapperData.append({key, attr.value().toString()});
                }
            }
        } else if (xml.name() == QLatin1String("mxCell")) {
            cells->append(Cell());
            current = &cells->last();
            current->id = a.hasAttribute(QLatin1String("id")) ? a.value(QLatin1String("id")).toString() : wrapperId;
            current->parent = a.value(QLatin1String("parent")).toString();
            current->value = a.hasAttribute(QLatin1String("value")) ? a.value(QLatin1String("value")).toString() : wrapperLabel;
            current->style = a.value(QLatin1String("style")).toString();
            current->vertex = a.value(QLatin1String("vertex")) == QLatin1String("1");
            current->edge = a.value(QLatin1String("edge")) == QLatin1String("1");
            current->source = a.value(QLatin1String("source")).toString();
            current->target = a.value(QLatin1String("target")).toString();
            current->data = wrapperData;
        } else if (xml.name() == QLatin1String("mxGeometry") && current != nullptr) {
            current->geometry = QRectF(a.value(QLatin1String("x")).toDouble(), a.value(QLatin1String("y")).toDouble(),
                                       a.value(QLatin1String("width")).toDouble(), a.value(QLatin1String("height")).toDouble());
            current->relative = a.value(QLatin1String("relative")) == QLatin1String("1");
        } else if (xml.name() == QLatin1String("mxPoint") && current != nullptr) {
            const QPointF p(a.value(QLatin1String("x")).toDouble(), a.value(QLatin1String("y")).toDouble());
            const auto as = a.value(QLatin1String("as"));
            if (as == QLatin1String("sourcePoint")) current->sourcePoint = p;
            else if (as == QLatin1String("targetPoint")) current->targetPoint = p;
        }
    }
    return !xml.hasError();
}

Page buildPage(Drawing &drawing, const QVector<Cell> &cells, const QString &name, QSizeF size, const QColor &background,
               QStringList *warnings)
{
    const Theme &palette = theme(drawing.themeId);
    Page page;
    page.id = drawing.newId(QStringLiteral("p"));
    page.name = name;
    page.size = size;
    page.background = background;
    QHash<QString, int> byId;
    for (int i = 0; i < cells.size(); ++i) byId.insert(cells.at(i).id, i);
    // Absolute origin of a cell: the sum of its vertex ancestors' positions.
    const auto origin = [&](const QString &parentId) {
        QPointF offset;
        QString p = parentId;
        for (int guard = 0; guard < 64 && byId.contains(p); ++guard) {
            const Cell &parent = cells.at(byId.value(p));
            if (!parent.vertex) break;
            offset += parent.geometry.topLeft();
            p = parent.parent;
        }
        return offset;
    };
    QHash<QString, QString> shapeIds; // draw.io id -> our id
    for (const Cell &cell : cells) {
        if (!cell.vertex || cell.edge) continue;
        if (byId.contains(cell.parent) && cells.at(byId.value(cell.parent)).edge) continue; // edge label
        QString shapeName;
        const Style style = parseStyle(cell.style, &shapeName);
        const QString type = stencilFor(style, shapeName);
        const Stencil *stencil = Diagram::stencil(type);
        Shape shape;
        shape.id = drawing.newId(QStringLiteral("s"));
        shape.type = type;
        shape.rect = QRectF(origin(cell.parent) + cell.geometry.topLeft(),
                            QSizeF(std::max(4.0, cell.geometry.width()), std::max(4.0, cell.geometry.height())));
        shape.text = plainText(cell.value, style);
        shape.style = shapeStyleFor(palette, stencil);
        shape.style.fill = colorFrom(style, "fillColor", type == QLatin1String("text") ? QColor(Qt::transparent) : QColor(Qt::white));
        shape.style.gradient = colorFrom(style, "gradientColor", QColor());
        shape.style.line = colorFrom(style, "strokeColor", type == QLatin1String("text") ? QColor(Qt::transparent) : QColor(0, 0, 0));
        shape.style.lineWidth = numberFrom(style, "strokeWidth", 1);
        shape.style.pattern = patternFrom(style);
        shape.style.shadow = style.value(QStringLiteral("shadow")) == QLatin1String("1");
        shape.style.opacity = std::clamp(numberFrom(style, "opacity", 100) / 100.0, 0.0, 1.0);
        shape.textStyle = textStyleFor(palette, stencil);
        shape.textStyle.color = QColor(0, 0, 0);
        shape.textStyle.size = 9;
        applyTextStyle(shape.textStyle, style);
        shape.rotation = numberFrom(style, "rotation", 0);
        shape.flipX = style.value(QStringLiteral("flipH")) == QLatin1String("1");
        shape.flipY = style.value(QStringLiteral("flipV")) == QLatin1String("1");
        shape.data = cell.data;
        const QString image = style.value(QStringLiteral("image"));
        if (type == QLatin1String("picture") && image.startsWith(QLatin1String("data:"))) {
            shape.image = QByteArray::fromBase64(image.section(QLatin1Char(','), 1).toLatin1());
        }
        shapeIds.insert(cell.id, shape.id);
        page.shapes.append(shape);
    }
    // Containers first so what is inside them paints on top.
    std::stable_sort(page.shapes.begin(), page.shapes.end(),
                     [](const Shape &a, const Shape &b) { return isContainer(a) && !isContainer(b); });
    QHash<QString, QString> connectorIds;
    for (const Cell &cell : cells) {
        if (!cell.edge) continue;
        QString shapeName;
        const Style style = parseStyle(cell.style, &shapeName);
        Connector c;
        c.id = drawing.newId(QStringLiteral("c"));
        styleConnector(palette, c);
        const QPointF offset = origin(cell.parent);
        const auto end = [&](const QString &id, QPointF point, const char *xKey, const char *yKey) {
            Endpoint e;
            e.point = offset + point;
            const QString shapeId = shapeIds.value(id);
            if (shapeId.isEmpty()) return e;
            e.shapeId = shapeId;
            if (style.contains(QLatin1String(xKey)) && style.contains(QLatin1String(yKey))) {
                const Shape *shape = page.shape(shapeId);
                const QPointF n(numberFrom(style, xKey, 0.5), numberFrom(style, yKey, 0.5));
                const QPointF at = shapeTransform(*shape).map(QPointF(n.x() * shape->rect.width(), n.y() * shape->rect.height()));
                e.port = nearestPort(*shape, at, std::max(shape->rect.width(), shape->rect.height()) * 0.2);
            }
            return e;
        };
        c.from = end(cell.source, cell.sourcePoint, "exitX", "exitY");
        c.to = end(cell.target, cell.targetPoint, "entryX", "entryY");
        const QString edgeStyle = style.value(QStringLiteral("edgeStyle"));
        c.routing = style.value(QStringLiteral("curved")) == QLatin1String("1") ? Routing::Curved
                  : edgeStyle.isEmpty() || edgeStyle == QLatin1String("none") ? Routing::Straight
                                                                              : Routing::Orthogonal;
        c.line = colorFrom(style, "strokeColor", QColor(0, 0, 0));
        c.lineWidth = numberFrom(style, "strokeWidth", 1);
        c.pattern = patternFrom(style);
        c.startArrow = arrowFrom(style.value(QStringLiteral("startArrow")), ArrowStyle::None);
        c.endArrow = arrowFrom(style.value(QStringLiteral("endArrow")), ArrowStyle::Filled);
        c.text = plainText(cell.value, style);
        c.textStyle.color = QColor(0, 0, 0);
        c.textStyle.size = 8.5;
        applyTextStyle(c.textStyle, style);
        c.data = cell.data;
        connectorIds.insert(cell.id, c.id);
        page.connectors.append(c);
    }
    // Edge labels are child vertices of the edge.
    for (const Cell &cell : cells) {
        if (cell.vertex && connectorIds.contains(cell.parent) && !cell.value.isEmpty()) {
            Connector *c = page.connector(connectorIds.value(cell.parent));
            const QString label = plainText(cell.value, parseStyle(cell.style, nullptr));
            c->text = c->text.isEmpty() ? label : c->text + QLatin1Char('\n') + label;
            if (cell.relative) c->labelPosition = std::clamp((cell.geometry.x() + 1) / 2, 0.0, 1.0);
        }
    }
    const QRectF content = itemsBounds(page);
    if (!content.isNull() && (content.right() > page.size.width() || content.bottom() > page.size.height())) {
        page.size = QSizeF(std::max(page.size.width(), std::ceil(content.right() + 40)),
                           std::max(page.size.height(), std::ceil(content.bottom() + 40)));
        warnings->append(tr("Page \"%1\" was enlarged to hold its content.").arg(name));
    }
    return page;
}

} // namespace

bool importDrawio(Drawing &drawing, const QByteArray &xmlBytes, QVector<Page> *pages, QStringList *warnings,
                  QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) *error = message;
        return false;
    };
    pages->clear();
    QXmlStreamReader xml(xmlBytes);
    QString pageName;
    QStringList notes;
    int index = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            if (xml.isCharacters() && !xml.isWhitespace() && !pageName.isNull()) {
                return fail(tr("This draw.io file is compressed. In diagrams.net, turn off File ▸ Properties ▸ "
                               "Compressed, save, and import it again."));
            }
            continue;
        }
        if (xml.name() == QLatin1String("diagram")) {
            pageName = xml.attributes().value(QLatin1String("name")).toString();
            if (pageName.isNull()) pageName = QStringLiteral("");
        } else if (xml.name() == QLatin1String("mxGraphModel")) {
            QVector<Cell> cells;
            QSizeF size(850, 1100);
            QColor background(Qt::white);
            if (!readModel(xml, &cells, &size, &background)) {
                return fail(tr("The draw.io file is damaged: %1").arg(xml.errorString()));
            }
            ++index;
            const QString name = pageName.isEmpty() ? tr("Page-%1").arg(index) : pageName;
            pages->append(buildPage(drawing, cells, name, size, background, &notes));
            pageName = QString();
        }
    }
    if (xml.hasError()) {
        return fail(tr("This is not a draw.io file: %1").arg(xml.errorString()));
    }
    if (pages->isEmpty()) {
        return fail(tr("The file holds no draw.io diagram."));
    }
    if (warnings != nullptr) *warnings = notes;
    return true;
}

} // namespace QindaOffice::Diagram
