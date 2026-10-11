// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation.h"
#include "math_omml.h"
#include "math_linear.h"

#include <QRegularExpression>

#include <cmath>

namespace QindaOffice::Show {

namespace {

void appendParagraphs(QString &out, const QList<TextParagraph> &paragraphs)
{
    for (const TextParagraph &paragraph : paragraphs) {
        for (const TextRun &run : paragraph.runs) {
            out += run.text;
        }
        out += QLatin1Char('\n');
    }
}

} // namespace

int effectiveDirection(ShapeAnimation animation, int direction)
{
    if (direction != 0) {
        return direction;
    }
    switch (animation) {
    case ShapeAnimation::FlyInLeft: return Direction::Left;
    case ShapeAnimation::FlyInRight: return Direction::Right;
    case ShapeAnimation::FlyInTop: return Direction::Top;
    case ShapeAnimation::FlyInBottom: return Direction::Bottom;
    case ShapeAnimation::Wipe: return Direction::Bottom;
    case ShapeAnimation::Split: return Direction::VerticalOut;
    case ShapeAnimation::Peek: return Direction::Bottom;
    case ShapeAnimation::Float: return Direction::Top; // Float Up: the way it moves
    default: return 0;
    }
}

ShapeAnimation flyInFor(int direction)
{
    if ((direction & Direction::Top) != 0) {
        return ShapeAnimation::FlyInTop;
    }
    if (direction == Direction::Left) {
        return ShapeAnimation::FlyInLeft;
    }
    if (direction == Direction::Right) {
        return ShapeAnimation::FlyInRight;
    }
    return ShapeAnimation::FlyInBottom;
}

bool PreservedXml::operator==(const PreservedXml &other) const
{
    if (label != other.label) {
        return false;
    }
    if (fragment == other.fragment) {
        return true;
    }
    // Serialised fragments spell relationship ids with the conventional r:
    // prefix (ooxml_namespaces.h); blank their values and compare the rest.
    static const QRegularExpression kRelationshipId(QStringLiteral("\\br:[A-Za-z]+=\"[^\"]*\""));
    QString mine = fragment;
    QString theirs = other.fragment;
    return mine.replace(kRelationshipId, QStringLiteral("r:id=\"\"")) == theirs.replace(kRelationshipId, QStringLiteral("r:id=\"\""));
}

bool ChartSeries::operator==(const ChartSeries &other) const
{
    // Gaps are NaN, and NaN never equals itself: compare them as "both gaps".
    const auto sameValues = [](const QList<qreal> &a, const QList<qreal> &b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (qsizetype i = 0; i < a.size(); ++i) {
            const bool gapA = std::isnan(a.at(i));
            const bool gapB = std::isnan(b.at(i));
            if (gapA != gapB || (!gapA && a.at(i) != b.at(i))) {
                return false;
            }
        }
        return true;
    };
    return name == other.name && categories == other.categories && sameValues(values, other.values)
        && sameValues(xValues, other.xValues) && color == other.color && pointColors == other.pointColors
        && plot == other.plot;
}

bool ChartData::operator==(const ChartData &other) const
{
    return type == other.type && grouping == other.grouping && title == other.title && legend == other.legend
        && legendPosition == other.legendPosition && series == other.series && palette == other.palette
        && plots == other.plots && categoryLevels == other.categoryLevels;
}

namespace {

void overlayRegion(TableRegion &into, const TableRegion &region)
{
    if (region.fill.isValid()) {
        into.fill = region.fill;
    }
    if (region.text.isValid()) {
        into.text = region.text;
    }
    into.bold = into.bold || region.bold;
    if (region.insideH.set) {
        into.insideH = region.insideH;
    }
    if (region.insideV.set) {
        into.insideV = region.insideV;
    }
    if (region.outer.set) {
        into.outer = region.outer;
    }
}

} // namespace

TableRegion Table::regionAt(int row, int column) const
{
    const int rowCount = int(rows.size());
    const int columnCount = int(columns.size());
    TableRegion region = look.whole;
    if (bandColumns) {
        const int index = column - (firstColumn ? 1 : 0);
        if (index >= 0) {
            overlayRegion(region, index % 2 == 0 ? look.band1V : look.band2V);
        }
    }
    if (bandRows) {
        const int index = row - (firstRow ? 1 : 0);
        if (index >= 0) {
            overlayRegion(region, index % 2 == 0 ? look.band1H : look.band2H);
        }
    }
    if (lastColumn && column == columnCount - 1) {
        overlayRegion(region, look.lastCol);
    }
    if (firstColumn && column == 0) {
        overlayRegion(region, look.firstCol);
    }
    if (lastRow && row == rowCount - 1) {
        overlayRegion(region, look.lastRow);
    }
    if (firstRow && row == 0) {
        overlayRegion(region, look.firstRow);
    }
    return region;
}

QString Shape::displayText() const
{
    if (kind == ShapeKind::Equation) {
        const auto equation = Math::readOmmlFragment(equationXml);
        return equation ? Math::toLinear(*equation) : QString();
    }
    QString out;
    appendParagraphs(out, paragraphs);
    for (const TableRow &row : table.rows) {
        for (const TableCell &cell : row.cells) {
            appendParagraphs(out, cell.paragraphs);
        }
    }
    if (kind == ShapeKind::Group) {
        for (const Shape &child : children) {
            out += child.displayText() + QLatin1Char('\n');
        }
    }
    return out.trimmed();
}

bool Shape::holdsText() const
{
    switch (kind) {
    case ShapeKind::TextBox:
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
    case ShapeKind::AutoShape:
        return true;
    case ShapeKind::Line:
    case ShapeKind::Arrow:
    case ShapeKind::Image:
    case ShapeKind::Table:
    case ShapeKind::Chart:
    case ShapeKind::Group:
    case ShapeKind::Equation:
    case ShapeKind::Preserved:
        return false;
    }
    return false;
}

Shape *Slide::findShape(const QString &shapeId)
{
    for (Shape &shape : shapes) {
        if (shape.id == shapeId) {
            return &shape;
        }
    }
    return nullptr;
}

Slide &Presentation::addSlide(const QString &slideTitle)
{
    Slide slide;
    slide.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    slide.title = slideTitle;
    slides.append(slide);
    return slides.last();
}

Slide *Presentation::findSlide(const QString &slideId)
{
    for (Slide &slide : slides) {
        if (slide.id == slideId) {
            return &slide;
        }
    }
    return nullptr;
}

QList<QString> Slide::shapeIdsInGroup(const QString &groupId) const
{
    QList<QString> ids;
    if (groupId.isEmpty()) {
        return ids;
    }
    for (const Shape &shape : shapes) {
        if (shape.groupId == groupId) {
            ids.append(shape.id);
        }
    }
    return ids;
}

const SlideMaster *Presentation::findMaster(const QString &id) const
{
    for (const SlideMaster &master : masters) {
        if (master.id == id) {
            return &master;
        }
    }
    return nullptr;
}

const SlideLayout *Presentation::findLayout(const QString &id) const
{
    for (const SlideLayout &layout : layouts) {
        if (layout.id == id) {
            return &layout;
        }
    }
    return nullptr;
}

const SlideMaster *Presentation::masterFor(const Slide &slide) const
{
    return slide.master.isEmpty() ? nullptr : findMaster(slide.master);
}

const SlideLayout *Presentation::layoutFor(const Slide &slide) const
{
    return slide.layoutId.isEmpty() ? nullptr : findLayout(slide.layoutId);
}

} // namespace QindaOffice::Show
