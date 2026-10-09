// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_reader.h"

#include "fodp_extended.h"
#include "fodp_read_styles.h"
#include "fodp_read_text.h"
#include "odf_dom.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

namespace QindaOffice::Show {

namespace {

using namespace Odf;
using Fodp::StyleSheet;

QColor colorOrInvalid(const QString &value)
{
    if (value.isEmpty() || value == u"none") {
        return {};
    }
    return QColor::fromString(value);
}

QRectF rectFrom(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 4) {
        return {};
    }
    return QRectF(parts.at(0).toDouble(), parts.at(1).toDouble(), parts.at(2).toDouble(), parts.at(3).toDouble());
}

qreal percentValue(const QString &value)
{
    QString number = value;
    number.remove(QLatin1Char('%'));
    bool ok = false;
    const qreal parsed = number.toDouble(&ok);
    return ok ? parsed : 100.0;
}

class Reader {
public:
    explicit Reader(const OdfNode &document)
        : m_sheet(document)
    {
    }

    Presentation read(const OdfNode &document);

private:
    void readShapes(const OdfNode &container, Slide &slide, const QString &groupId);
    bool readShape(const OdfNode &node, Shape &shape);
    void readGeometry(const OdfNode &node, Shape &shape) const;
    void readGraphicStyle(const OdfNode &node, Shape &shape) const;
    void readExtras(const OdfNode &node, Shape &shape) const;
    [[nodiscard]] QString graphicStyleName(const OdfNode &node, QString *family) const;
    [[nodiscard]] QString transitionOf(const QString &pageStyle) const;

    StyleSheet m_sheet;
    int m_groups = 0;
};

QString Reader::graphicStyleName(const OdfNode &node, QString *family) const
{
    QString name = node.attr(kDraw, u"style-name");
    *family = QStringLiteral("graphic");
    if (name.isEmpty() && node.hasAttr(kPresentation, u"style-name")) {
        name = node.attr(kPresentation, u"style-name");
        *family = QStringLiteral("presentation");
    }
    // Pre-M62 QindaShow wrote a "shape-<name>" style per shape but never
    // linked it; adopt it, so old files get their fills back.
    if (name.isEmpty()) {
        const QString legacy = QStringLiteral("shape-") + node.attr(kDraw, u"name");
        if (m_sheet.hasStyle(legacy, QStringLiteral("graphic"))) {
            name = legacy;
        }
    }
    return name;
}

void Reader::readGraphicStyle(const OdfNode &node, Shape &shape) const
{
    QString family;
    const QString style = graphicStyleName(node, &family);
    const auto prop = [&](QStringView ns, QStringView local) {
        return m_sheet.property(style, family, u"graphic-properties", ns, local);
    };
    // The model's exact values when this file is ours…
    const QString exactFill = prop(kQq, u"fill-color");
    if (!exactFill.isEmpty()) {
        shape.fill = colorOrInvalid(exactFill);
        shape.stroke = colorOrInvalid(prop(kQq, u"stroke-color"));
        shape.strokeWidth = prop(kQq, u"stroke-width").toDouble();
        return;
    }
    // …otherwise what ODF says (LibreOffice files, pre-M62 QindaShow).
    const QString fill = prop(kDraw, u"fill");
    if (fill == u"solid" || fill == u"gradient" || fill == u"hatch" || fill == u"bitmap") {
        QColor color = colorOrInvalid(prop(kDraw, u"fill-color"));
        if (!color.isValid()) {
            color = QColor(0x72, 0x9f, 0xcf); // ODF's default fill colour
        }
        const QString opacity = prop(kDraw, u"opacity");
        if (!opacity.isEmpty()) {
            color.setAlphaF(float(qBound(0.0, percentValue(opacity) / 100.0, 1.0)));
        }
        shape.fill = color;
    } else if (fill == u"none") {
        shape.fill = Qt::transparent;
    } else if (const QColor legacy = colorOrInvalid(prop(kDraw, u"fill-color")); legacy.isValid() && fill.isEmpty()
               && style.startsWith(QLatin1String("shape-"))) {
        shape.fill = legacy;
    }
    const QString stroke = prop(kDraw, u"stroke");
    if (stroke == u"none") {
        shape.strokeWidth = 0;
    } else {
        const QColor color = colorOrInvalid(prop(kSvg, u"stroke-color"));
        if (color.isValid()) {
            shape.stroke = color;
        }
        const QString width = prop(kSvg, u"stroke-width");
        if (!width.isEmpty()) {
            // ODF's 0 width is a hairline, not "no line".
            shape.strokeWidth = qMax<qreal>(lengthToPt(width), 0.75);
        }
        const QString opacity = prop(kSvg, u"stroke-opacity");
        if (!opacity.isEmpty() && shape.stroke.isValid()) {
            shape.stroke.setAlphaF(float(qBound(0.0, percentValue(opacity) / 100.0, 1.0)));
        }
    }
}

void Reader::readGeometry(const OdfNode &node, Shape &shape) const
{
    const qreal width = lengthToPt(node.attr(kSvg, u"width"));
    const qreal height = lengthToPt(node.attr(kSvg, u"height"));
    const qreal x = lengthToPt(node.attr(kSvg, u"x"));
    const qreal y = lengthToPt(node.attr(kSvg, u"y"));
    shape.rect = QRectF(x, y, width, height);
    const QString transform = node.attr(kDraw, u"transform");
    if (!transform.isEmpty()) {
        if (!transform.contains(QLatin1String("translate")) && node.hasAttr(kSvg, u"x")) {
            // Pre-M62 QindaShow: svg:x/y kept, "rotate (a)" with the model's
            // CLOCKWISE angle in radians (degrees in the oldest files).
            const QString inner = transform.section(QLatin1Char('('), 1).section(QLatin1Char(')'), 0, 0);
            const double raw = inner.trimmed().toDouble();
            shape.rotation = qFabs(raw) > 2.0 * M_PI + 0.01 ? raw : qRadiansToDegrees(raw);
        } else {
            bool ok = false;
            const QTransform matrix = parseTransform(transform, &ok);
            if (ok) {
                const QPointF origin = matrix.map(QPointF(0, 0));
                const QPointF xAxis = matrix.map(QPointF(1, 0)) - origin;
                const QPointF yAxis = matrix.map(QPointF(0, 1)) - origin;
                const qreal sx = std::hypot(xAxis.x(), xAxis.y());
                const qreal sy = std::hypot(yAxis.x(), yAxis.y());
                const QPointF centre = matrix.map(QPointF(width / 2.0, height / 2.0));
                const QSizeF size(width * sx, height * sy);
                shape.rect = QRectF(centre - QPointF(size.width() / 2.0, size.height() / 2.0), size);
                qreal degrees = qRadiansToDegrees(std::atan2(xAxis.y(), xAxis.x()));
                if (degrees < 0) {
                    degrees += 360.0;
                }
                shape.rotation = qFuzzyIsNull(degrees) || qFuzzyCompare(degrees, 360.0) ? 0.0 : degrees;
            }
        }
    }
    if (node.hasAttr(kQq, u"rotation")) {
        shape.rotation = node.attr(kQq, u"rotation").toDouble();
    }
    if (node.hasAttr(kQq, u"rect")) {
        shape.rect = rectFrom(node.attr(kQq, u"rect"));
    }
}

// The qq: attributes ODF has no home for; paired with writeShapeExtras in
// fodp_writer.cpp.
void Reader::readExtras(const OdfNode &node, Shape &shape) const
{
    shape.id = node.attr(kDraw, u"name");
    shape.buildStep = node.attr(kQq, u"build-step").toInt();
    if (node.hasAttr(kQq, u"animation")) {
        const int animation = node.attr(kQq, u"animation").toInt();
        shape.animation = ShapeAnimation(qBound(0, animation, int(kLastAnimation)));
    }
    if (node.hasAttr(kQq, u"animation-ms")) {
        const int ms = node.attr(kQq, u"animation-ms").toInt();
        shape.animationMs = ms > 0 ? qBound(50, ms, 5000) : 350;
    }
    shape.groupId = node.attr(kQq, u"group");
    const QString extra = node.attr(kQq, u"extra");
    if (!extra.isEmpty()) {
        shape.extra = QJsonDocument::fromJson(extra.toUtf8()).object().toVariantMap();
    }
}

bool Reader::readShape(const OdfNode &node, Shape &shape)
{
    // The exact shape QindaShow wrote beside the ODF approximation.
    if (node.hasAttr(kQq, u"shape")) {
        if (std::optional<Shape> exact = Fodp::fromExtendedCopy(node.attr(kQq, u"shape"), node.attr(kDraw, u"name"))) {
            shape = *exact;
            shape.groupId = node.attr(kQq, u"group");
            return true;
        }
    }
    QString family;
    const QString style = graphicStyleName(node, &family);
    Fodp::TextDefaults text{node.attr(kDraw, u"text-style-name"), style, family};
    readExtras(node, shape);
    readGraphicStyle(node, shape);
    if (node.is(kDraw, u"line") || node.is(kDraw, u"connector")) {
        QPointF from(lengthToPt(node.attr(kSvg, u"x1")), lengthToPt(node.attr(kSvg, u"y1")));
        QPointF to(lengthToPt(node.attr(kSvg, u"x2")), lengthToPt(node.attr(kSvg, u"y2")));
        const bool startMarker = !m_sheet.property(style, family, u"graphic-properties", kDraw, u"marker-start").isEmpty();
        const bool endMarker = !m_sheet.property(style, family, u"graphic-properties", kDraw, u"marker-end").isEmpty();
        if (startMarker && !endMarker) {
            std::swap(from, to); // the model's arrowhead sits at the end
        }
        shape.kind = startMarker || endMarker ? ShapeKind::Arrow : ShapeKind::Line;
        // Un-normalised on purpose: the direction is the sign of w/h.
        shape.rect = QRectF(from, to);
        if (node.hasAttr(kQq, u"rect")) {
            shape.rect = rectFrom(node.attr(kQq, u"rect"));
            shape.rotation = node.attr(kQq, u"rotation").toDouble();
        }
        return true;
    }
    readGeometry(node, shape);
    if (node.is(kDraw, u"frame")) {
        if (const OdfNode *image = node.child(kDraw, u"image")) {
            const OdfNode *binary = image->child(kOffice, u"binary-data");
            if (binary != nullptr) {
                QString base64;
                for (const OdfNodePtr &c : binary->children) {
                    base64 += c->text;
                }
                shape.kind = ShapeKind::Image;
                shape.imagePng = QByteArray::fromBase64(base64.toLatin1());
                return true;
            }
        }
        if (const OdfNode *box = node.child(kDraw, u"text-box")) {
            shape.kind = ShapeKind::TextBox;
            shape.paragraphs = Fodp::readParagraphs(*box, m_sheet, text);
            return true;
        }
        return false; // objects, tables, plug-ins: nothing the model can show
    }
    shape.kind = node.is(kDraw, u"ellipse") || node.is(kDraw, u"circle") ? ShapeKind::Ellipse : ShapeKind::Rectangle;
    if (const OdfNode *geometry = node.child(kDraw, u"enhanced-geometry")) {
        const QString type = geometry->attr(kDraw, u"type");
        if (type == u"ellipse" || type == u"ooxml-ellipse" || type == u"circle") {
            shape.kind = ShapeKind::Ellipse;
        } else if (const QString preset = Fodp::presetForOdfType(type); !preset.isEmpty() && preset != u"rect") {
            // A LibreOffice arrow, chevron, … keeps its outline.
            shape.kind = ShapeKind::AutoShape;
            shape.geometry = preset;
        }
    }
    shape.paragraphs = Fodp::readParagraphs(node, m_sheet, text);
    return true;
}

void Reader::readShapes(const OdfNode &container, Slide &slide, const QString &groupId)
{
    for (const OdfNode *node : container.elements()) {
        if (node->is(kDraw, u"g")) {
            // An ODF group becomes a QindaShow group: the members select and
            // move as one.
            readShapes(*node, slide, groupId.isEmpty() ? QStringLiteral("odf-group-%1").arg(++m_groups) : groupId);
            continue;
        }
        const bool shapeElement = node->is(kDraw, u"frame") || node->is(kDraw, u"custom-shape")
            || node->is(kDraw, u"rect") || node->is(kDraw, u"ellipse") || node->is(kDraw, u"circle")
            || node->is(kDraw, u"line") || node->is(kDraw, u"connector");
        if (!shapeElement) {
            continue;
        }
        Shape shape;
        if (!readShape(*node, shape)) {
            continue;
        }
        if (!groupId.isEmpty() && shape.groupId.isEmpty()) {
            shape.groupId = groupId;
        }
        slide.shapes.append(shape);
    }
}

QString Reader::transitionOf(const QString &pageStyle) const
{
    const QString family = QStringLiteral("drawing-page");
    const QString exact = m_sheet.property(pageStyle, family, u"drawing-page-properties", kQq, u"transition");
    if (!exact.isEmpty()) {
        return exact;
    }
    const QString type = m_sheet.property(pageStyle, family, u"drawing-page-properties", kSmil, u"type");
    if (type.isEmpty()) {
        return QStringLiteral("cut");
    }
    if (type == u"fade" || type == u"dissolve") {
        return QStringLiteral("fade");
    }
    if (type == u"barWipe") { // M67
        return QStringLiteral("wipe");
    }
    if (type == u"barnDoorWipe") {
        return QStringLiteral("split");
    }
    if (type == u"slide" || type == u"slideWipe" || type == u"pushWipe") {
        return QStringLiteral("slide");
    }
    if (type == u"zoom" || type == u"irisWipe" || type == u"ellipseWipe") {
        return QStringLiteral("zoom");
    }
    return QStringLiteral("fade"); // some effect the player approximates
}

Presentation Reader::read(const OdfNode &document)
{
    Presentation presentation;
    if (const OdfNode *meta = document.child(kOffice, u"meta")) {
        if (const OdfNode *title = meta->child(u"http://purl.org/dc/elements/1.1/", u"title")) {
            for (const OdfNodePtr &c : title->children) {
                presentation.title += c->text;
            }
        }
    }
    // Our masters (the ones carrying qq:background).
    QHash<QString, QString> masterIds; // master page name → model id
    for (const OdfNode *page : m_sheet.masterPages()) {
        if (!page->hasAttr(kQq, u"background")) {
            continue;
        }
        SlideMaster master;
        const QString name = page->attr(kStyle, u"name");
        master.id = page->attr(kQq, u"id", name);
        master.name = page->attr(kQq, u"master-name");
        master.background = QColor::fromString(page->attr(kQq, u"background"));
        master.titleColor = QColor::fromString(page->attr(kQq, u"title-color"));
        master.bodyColor = QColor::fromString(page->attr(kQq, u"body-color"));
        master.fontFamily = page->attr(kQq, u"font");
        master.typeDefaults = page->attr(kQq, u"type-defaults") != u"0";
        if (page->hasAttr(kQq, u"background-style")) {
            master.backgroundStyle = Fodp::backgroundFromJson(page->attr(kQq, u"background-style")).style;
        }
        if (page->hasAttr(kQq, u"shapes")) {
            master.shapes = Fodp::shapesFromJson(page->attr(kQq, u"shapes"));
        }
        if (page->hasAttr(kQq, u"theme")) {
            master.theme = Fodp::themeFromJson(page->attr(kQq, u"theme"));
        }
        if (page->hasAttr(kQq, u"text-styles")) {
            Fodp::textStylesFromJson(page->attr(kQq, u"text-styles"), master);
        }
        if (page->hasAttr(kQq, u"placeholders")) {
            master.placeholders = Fodp::placeholdersFromJson(page->attr(kQq, u"placeholders"));
        }
        if (page->hasAttr(kQq, u"order")) {
            master.order = page->attr(kQq, u"order").split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        presentation.masters.append(master);
        masterIds.insert(name, master.id);
    }
    const OdfNode *body = document.child(kOffice, u"body");
    const OdfNode *deck = body != nullptr ? body->child(kOffice, u"presentation") : nullptr;
    if (deck == nullptr) {
        return presentation;
    }
    presentation.layouts = Fodp::layoutsFromJson(deck->attr(kQq, u"layouts"));
    presentation.sections = Fodp::sectionsFromJson(deck->attr(kQq, u"sections"));
    presentation.legacyComments = deck->attr(kQq, u"legacy-comments") == u"1";
    if (deck->hasAttr(kQq, u"notes-size")) {
        const QStringList size = deck->attr(kQq, u"notes-size").split(QLatin1Char(' '));
        if (size.size() == 2) {
            presentation.notesSize = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
        }
    }
    const QList<const OdfNode *> pages = deck->elements(kDraw, u"page");
    const QSizeF size = m_sheet.masterPageSize(pages.isEmpty() ? QString()
                                                                : pages.first()->attr(kDraw, u"master-page-name"));
    if (size.width() > 0 && size.height() > 0) {
        presentation.slideSize = size;
    }
    for (const OdfNode *page : pages) {
        Slide slide;
        slide.id = page->attr(kDraw, u"name");
        slide.title = page->attr(kQq, u"title");
        slide.layout = page->attr(kQq, u"layout", slide.layout);
        const QString masterPage = page->attr(kDraw, u"master-page-name");
        slide.master = page->attr(kQq, u"master", masterIds.value(masterPage));
        slide.transition = transitionOf(page->attr(kDraw, u"style-name"));
        slide.hidden = m_sheet.property(page->attr(kDraw, u"style-name"), QStringLiteral("drawing-page"),
                                        u"drawing-page-properties", kPresentation, u"visibility")
            == u"hidden";
        slide.showMasterShapes = page->attr(kQq, u"show-master") != u"0";
        if (page->hasAttr(kQq, u"background")) {
            slide.background = Fodp::backgroundFromJson(page->attr(kQq, u"background"));
        }
        slide.layoutId = page->attr(kQq, u"layout-id");
        if (page->hasAttr(kQq, u"comments")) {
            slide.comments = Fodp::commentsFromJson(page->attr(kQq, u"comments"));
        }
        readShapes(*page, slide, QString());
        if (const OdfNode *notes = page->child(kPresentation, u"notes")) {
            for (const OdfNode *frame : notes->elements(kDraw, u"frame")) {
                if (frame->attr(kPresentation, u"class") == u"notes") {
                    if (const OdfNode *box = frame->child(kDraw, u"text-box")) {
                        slide.notes = Fodp::readPlainText(*box).trimmed();
                    }
                }
            }
        }
        presentation.slides.append(slide);
    }
    return presentation;
}

} // namespace

Presentation readFodp(const QByteArray &fodp, QString *error)
{
    const OdfNodePtr document = parseOdf(fodp, error);
    if (!document) {
        return {};
    }
    if (error != nullptr) {
        error->clear();
    }
    Reader reader(*document);
    return reader.read(*document);
}

} // namespace QindaOffice::Show
