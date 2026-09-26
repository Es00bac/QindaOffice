// SPDX-License-Identifier: GPL-3.0-or-later
// Ops: grouping, duplication and paste, text/style/data, find and replace,
// pages.
#include "ops.h"

#include "drawing_io.h"
#include "geometry.h"
#include "painter.h"
#include "routing.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

namespace {

// Inserts copies of `shapes`/`connectors` with fresh ids, shifted by
// `offset`; glue between copied items follows the copies, glue to anything
// else becomes a free end where it was.
QStringList insertCopies(Drawing &drawing, Page &page, QVector<Shape> shapes, QVector<Connector> connectors,
                         QPointF offset)
{
    QHash<QString, QString> ids;
    QHash<QString, QString> groups;
    QStringList out;
    for (Shape &shape : shapes) {
        const QString fresh = drawing.newId(QStringLiteral("s"));
        ids.insert(shape.id, fresh);
        shape.id = fresh;
        shape.rect.translate(offset);
        if (!shape.groupId.isEmpty()) {
            if (!groups.contains(shape.groupId)) {
                groups.insert(shape.groupId, drawing.newId(QStringLiteral("g")));
            }
            shape.groupId = groups.value(shape.groupId);
        }
    }
    for (Connector &connector : connectors) {
        // fragmentOf stored every glued end's current point in `point`.
        const auto remap = [&](Endpoint &end) {
            if (end.glued() && ids.contains(end.shapeId)) {
                end.shapeId = ids.value(end.shapeId);
            } else {
                end = Endpoint{QString(), -1, end.point + offset};
            }
        };
        remap(connector.from);
        remap(connector.to);
        connector.id = drawing.newId(QStringLiteral("c"));
        if (!connector.groupId.isEmpty()) {
            if (!groups.contains(connector.groupId)) {
                groups.insert(connector.groupId, drawing.newId(QStringLiteral("g")));
            }
            connector.groupId = groups.value(connector.groupId);
        }
    }
    for (const Shape &shape : std::as_const(shapes)) {
        page.shapes.append(shape);
        out.append(shape.id);
    }
    for (const Connector &connector : std::as_const(connectors)) {
        page.connectors.append(connector);
        out.append(connector.id);
    }
    return out;
}

bool patchShape(ShapeStyle &style, const ShapeStylePatch &patch)
{
    const ShapeStyle before = style;
    if (patch.fill) style.fill = *patch.fill;
    if (patch.gradient) style.gradient = *patch.gradient;
    if (patch.line) style.line = *patch.line;
    if (patch.lineWidth) style.lineWidth = std::clamp(*patch.lineWidth, 0.0, 48.0);
    if (patch.pattern) style.pattern = *patch.pattern;
    if (patch.cornerRadius) style.cornerRadius = std::clamp(*patch.cornerRadius, 0.0, 400.0);
    if (patch.shadow) style.shadow = *patch.shadow;
    if (patch.opacity) style.opacity = std::clamp(*patch.opacity, 0.0, 1.0);
    return !(style == before);
}

bool patchText(TextStyle &text, const TextStylePatch &patch)
{
    const TextStyle before = text;
    if (patch.family) text.family = *patch.family;
    if (patch.size) text.size = std::clamp(*patch.size, 4.0, 200.0);
    if (patch.bold) text.bold = *patch.bold;
    if (patch.italic) text.italic = *patch.italic;
    if (patch.underline) text.underline = *patch.underline;
    if (patch.color) text.color = *patch.color;
    if (patch.horizontal) text.align = (text.align & ~Qt::AlignHorizontal_Mask) | (*patch.horizontal & Qt::AlignHorizontal_Mask);
    if (patch.vertical) text.align = (text.align & ~Qt::AlignVertical_Mask) | (*patch.vertical & Qt::AlignVertical_Mask);
    return !(text == before);
}

QList<DataField> *dataOf(Page &page, const QString &id)
{
    if (Shape *shape = page.shape(id)) {
        return &shape->data;
    }
    if (Connector *connector = page.connector(id)) {
        return &connector->data;
    }
    return nullptr;
}

} // namespace

QString group(Drawing &drawing, Page &page, const QStringList &ids)
{
    int count = 0;
    for (const Shape &shape : page.shapes) {
        count += ids.contains(shape.id) ? 1 : 0;
    }
    for (const Connector &connector : page.connectors) {
        count += ids.contains(connector.id) ? 1 : 0;
    }
    if (count < 2) {
        return {};
    }
    const QString id = drawing.newId(QStringLiteral("g"));
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id)) {
            shape.groupId = id;
        }
    }
    for (Connector &connector : page.connectors) {
        if (ids.contains(connector.id)) {
            connector.groupId = id;
        }
    }
    return id;
}

bool ungroup(Page &page, const QStringList &ids)
{
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id) && !shape.groupId.isEmpty()) {
            shape.groupId.clear();
            changed = true;
        }
    }
    for (Connector &connector : page.connectors) {
        if (ids.contains(connector.id) && !connector.groupId.isEmpty()) {
            connector.groupId.clear();
            changed = true;
        }
    }
    return changed;
}

QStringList duplicate(Drawing &drawing, Page &page, const QStringList &ids, QPointF offset)
{
    QVector<Shape> shapes;
    QVector<Connector> connectors;
    QString error;
    if (!parseFragment(fragmentOf(page, ids), &shapes, &connectors, &error)) {
        return {};
    }
    return insertCopies(drawing, page, shapes, connectors, offset);
}

QStringList paste(Drawing &drawing, Page &page, const QByteArray &fragment, QPointF offset, QString *error)
{
    QVector<Shape> shapes;
    QVector<Connector> connectors;
    if (!parseFragment(fragment, &shapes, &connectors, error)) {
        return {};
    }
    return insertCopies(drawing, page, shapes, connectors, offset);
}

bool setText(Page &page, const QString &id, const QString &text)
{
    if (Shape *shape = page.shape(id)) {
        if (shape->text == text) {
            return false;
        }
        shape->text = text;
        return true;
    }
    if (Connector *connector = page.connector(id)) {
        if (connector->text == text) {
            return false;
        }
        connector->text = text;
        return true;
    }
    return false;
}

bool applyShapeStyle(Page &page, const QStringList &ids, const ShapeStylePatch &patch)
{
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id)) {
            changed = patchShape(shape.style, patch) || changed;
        }
    }
    // Lines apply to connectors too (line colour, weight and pattern).
    for (Connector &connector : page.connectors) {
        if (!ids.contains(connector.id)) {
            continue;
        }
        const Connector before = connector;
        if (patch.line) connector.line = *patch.line;
        if (patch.lineWidth) connector.lineWidth = std::clamp(*patch.lineWidth, 0.25, 48.0);
        if (patch.pattern) connector.pattern = *patch.pattern;
        changed = changed || !(connector == before);
    }
    return changed;
}

bool applyTextStyle(Page &page, const QStringList &ids, const TextStylePatch &patch)
{
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id)) {
            changed = patchText(shape.textStyle, patch) || changed;
        }
    }
    for (Connector &connector : page.connectors) {
        if (ids.contains(connector.id)) {
            changed = patchText(connector.textStyle, patch) || changed;
        }
    }
    return changed;
}

bool applyConnectorPatch(Page &page, const QStringList &ids, const ConnectorPatch &patch)
{
    bool changed = false;
    for (Connector &connector : page.connectors) {
        if (!ids.contains(connector.id)) {
            continue;
        }
        const Connector before = connector;
        if (patch.routing) connector.routing = *patch.routing;
        if (patch.startArrow) connector.startArrow = *patch.startArrow;
        if (patch.endArrow) connector.endArrow = *patch.endArrow;
        if (patch.labelPosition) connector.labelPosition = std::clamp(*patch.labelPosition, 0.0, 1.0);
        changed = changed || !(connector == before);
    }
    return changed;
}

bool copyFormat(Page &page, const QString &source, const QStringList &targets)
{
    bool changed = false;
    if (const Shape *from = page.shape(source)) {
        const Shape model = *from;
        for (Shape &shape : page.shapes) {
            if (targets.contains(shape.id) && shape.id != source) {
                const ShapeStyle beforeStyle = shape.style;
                const TextStyle beforeText = shape.textStyle;
                shape.style = model.style;
                shape.textStyle = model.textStyle;
                changed = changed || !(beforeStyle == shape.style) || !(beforeText == shape.textStyle);
            }
        }
        for (Connector &connector : page.connectors) {
            if (targets.contains(connector.id)) {
                const Connector before = connector;
                connector.line = model.style.line;
                connector.lineWidth = std::max(0.25, model.style.lineWidth);
                connector.pattern = model.style.pattern;
                connector.textStyle = model.textStyle;
                changed = changed || !(connector == before);
            }
        }
        return changed;
    }
    const Connector *from = page.connector(source);
    if (from == nullptr) {
        return false;
    }
    const Connector model = *from;
    for (Connector &connector : page.connectors) {
        if (targets.contains(connector.id) && connector.id != source) {
            const Connector before = connector;
            connector.line = model.line;
            connector.lineWidth = model.lineWidth;
            connector.pattern = model.pattern;
            connector.startArrow = model.startArrow;
            connector.endArrow = model.endArrow;
            connector.routing = model.routing;
            connector.textStyle = model.textStyle;
            changed = changed || !(connector == before);
        }
    }
    for (Shape &shape : page.shapes) {
        if (targets.contains(shape.id)) {
            const ShapeStyle before = shape.style;
            shape.style.line = model.line;
            shape.style.lineWidth = model.lineWidth;
            shape.style.pattern = model.pattern;
            changed = changed || !(before == shape.style);
        }
    }
    return changed;
}

bool setData(Page &page, const QString &id, const QString &key, const QString &value)
{
    QList<DataField> *data = dataOf(page, id);
    if (data == nullptr) {
        return false;
    }
    const QList<DataField> before = *data;
    return setDataValue(*data, key, value) && !(before == *data);
}

bool removeData(Page &page, const QString &id, const QString &key)
{
    QList<DataField> *data = dataOf(page, id);
    if (data == nullptr) {
        return false;
    }
    const qsizetype before = data->size();
    data->removeIf([&](const DataField &field) { return field.key.compare(key, Qt::CaseInsensitive) == 0; });
    return data->size() != before;
}

QStringList find(const Page &page, const QString &text)
{
    QStringList out;
    const QString needle = text.trimmed();
    if (needle.isEmpty()) {
        return out;
    }
    const auto matches = [&](const QString &value, const QList<DataField> &data) {
        if (value.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
        return std::any_of(data.begin(), data.end(),
                           [&](const DataField &f) { return f.value.contains(needle, Qt::CaseInsensitive); });
    };
    for (const Shape &shape : page.shapes) {
        if (matches(shape.text, shape.data)) {
            out.append(shape.id);
        }
    }
    for (const Connector &connector : page.connectors) {
        if (matches(connector.text, connector.data)) {
            out.append(connector.id);
        }
    }
    return out;
}

int replaceText(Page &page, const QString &find, const QString &replacement)
{
    if (find.isEmpty()) {
        return 0;
    }
    int count = 0;
    const auto replaceIn = [&](QString &text) {
        const int n = int(text.count(find, Qt::CaseInsensitive));
        if (n > 0) {
            text.replace(find, replacement, Qt::CaseInsensitive);
            count += n;
        }
    };
    for (Shape &shape : page.shapes) {
        replaceIn(shape.text);
    }
    for (Connector &connector : page.connectors) {
        replaceIn(connector.text);
    }
    return count;
}

QString addPage(Drawing &drawing, int index, const QString &name, const Page *like)
{
    Page page;
    if (like != nullptr) {
        page.size = like->size;
        page.background = like->background;
        page.grid = like->grid;
        page.gridSpacing = like->gridSpacing;
        page.lineJumps = like->lineJumps;
    }
    page.id = drawing.newId(QStringLiteral("p"));
    page.name = name.trimmed().isEmpty()
        ? QCoreApplication::translate("Diagram", "Page-%1").arg(drawing.pages.size() + 1)
        : name.trimmed();
    drawing.pages.insert(std::clamp(index, 0, int(drawing.pages.size())), page);
    return page.id;
}

QString duplicatePage(Drawing &drawing, const QString &pageId)
{
    const int index = drawing.pageIndex(pageId);
    if (index < 0) {
        return {};
    }
    const Page source = drawing.pages.at(index);
    const QString id = addPage(drawing, index + 1,
                               QCoreApplication::translate("Diagram", "%1 (copy)").arg(source.name), &source);
    Page *copy = drawing.page(id);
    QStringList all;
    for (const Shape &shape : source.shapes) {
        all.append(shape.id);
    }
    for (const Connector &connector : source.connectors) {
        all.append(connector.id);
    }
    QVector<Shape> shapes;
    QVector<Connector> connectors;
    if (parseFragment(fragmentOf(source, all), &shapes, &connectors)) {
        insertCopies(drawing, *copy, shapes, connectors, QPointF());
    }
    return id;
}

bool removePage(Drawing &drawing, const QString &pageId)
{
    const int index = drawing.pageIndex(pageId);
    if (index < 0 || drawing.pages.size() <= 1) {
        return false;
    }
    drawing.pages.removeAt(index);
    return true;
}

bool movePage(Drawing &drawing, const QString &pageId, int toIndex)
{
    const int from = drawing.pageIndex(pageId);
    const int to = std::clamp(toIndex, 0, int(drawing.pages.size()) - 1);
    if (from < 0 || from == to) {
        return false;
    }
    drawing.pages.move(from, to);
    return true;
}

bool renamePage(Drawing &drawing, const QString &pageId, const QString &name)
{
    Page *page = drawing.page(pageId);
    const QString trimmed = name.trimmed();
    if (page == nullptr || trimmed.isEmpty() || page->name == trimmed) {
        return false;
    }
    page->name = trimmed;
    return true;
}

bool fitPageToContents(Page &page, double margin)
{
    const QRectF content = itemsBounds(page);
    if (content.isNull()) {
        return false;
    }
    const QPointF shift = QPointF(margin, margin) - content.topLeft();
    for (Shape &shape : page.shapes) {
        shape.rect.translate(shift);
    }
    for (Connector &connector : page.connectors) {
        connector.from.point += shift;
        connector.to.point += shift;
    }
    page.size = QSizeF(std::ceil(content.width() + margin * 2), std::ceil(content.height() + margin * 2));
    return true;
}

} // namespace QindaOffice::Diagram::Ops
