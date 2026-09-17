// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_reader.h"

#include <QXmlStreamReader>

namespace QindaOffice::Show {

namespace {

qreal lengthToPt(QStringView value)
{
    // cm/mm/in/pt -> pt (plan section 7.5).
    if (value.endsWith(QLatin1String("cm"))) {
        return value.first(value.size() - 2).toDouble() * 72.0 / 2.54;
    }
    if (value.endsWith(QLatin1String("mm"))) {
        return value.first(value.size() - 2).toDouble() * 72.0 / 25.4;
    }
    if (value.endsWith(QLatin1String("in"))) {
        return value.first(value.size() - 2).toDouble() * 72.0;
    }
    if (value.endsWith(QLatin1String("pt"))) {
        return value.first(value.size() - 2).toDouble();
    }
    return value.toDouble(); // bare numbers read as pt
}

// Reads the qq: extension attributes ODF has no home for. A field the
// reader skips is a field the user loses on reopen, so this is paired with
// writeShapeExtras in fodp_writer.cpp.
void readShapeExtras(Shape &shape, const QXmlStreamAttributes &attrs)
{
    if (attrs.hasAttribute(QStringLiteral("qq:animation"))) {
        const int animation = attrs.value(QStringLiteral("qq:animation")).toInt();
        shape.animation = ShapeAnimation(qBound(0, animation, int(ShapeAnimation::Zoom)));
        const int ms = attrs.value(QStringLiteral("qq:animation-ms")).toInt();
        shape.animationMs = ms > 0 ? qBound(50, ms, 5000) : 350;
    }
    shape.groupId = attrs.value(QStringLiteral("qq:group")).toString();
}

struct GraphicStyle {
    QColor fill;
    QColor stroke;
    qreal strokeWidth = 1.0;
};

QColor colorFrom(QStringView value)
{
    return value.isEmpty() ? QColor() : QColor::fromString(value.toString());
}

TextRun readRun(const QXmlStreamAttributes &attrs, const QString &text)
{
    TextRun run;
    run.text = text;
    for (const QXmlStreamAttribute &attr : attrs) {
        const QStringView name = attr.name();
        const QStringView ns = attr.prefix();
        if (ns == QLatin1String("fo") && name == QLatin1String("font-weight")) {
            run.bold = attr.value() == QLatin1String("bold");
        } else if (ns == QLatin1String("fo") && name == QLatin1String("font-style")) {
            run.italic = attr.value() == QLatin1String("italic");
        } else if (ns == QLatin1String("fo") && name == QLatin1String("color")) {
            run.color = QColor::fromString(attr.value().toString());
        } else if (ns == QLatin1String("fo") && name == QLatin1String("font-size")) {
            run.pointSize = int(lengthToPt(attr.value()));
        } else if (ns == QLatin1String("style") && name == QLatin1String("font-name")) {
            run.fontFamily = attr.value().toString();
        }
    }
    return run;
}

} // namespace

Presentation readFodp(const QByteArray &fodp, QString *error)
{
    Presentation presentation;
    QXmlStreamReader xml(fodp);
    if (xml.hasError()) {
        if (error != nullptr) {
            *error = xml.errorString();
        }
        return presentation;
    }

    QHash<QString, GraphicStyle> styles;
    QHash<QString, QString> pageTransitions;
    Slide currentSlide;
    Shape currentShape;
    bool inSlide = false;
    bool inShape = false;
    bool inParagraph = false;
    TextParagraph currentParagraph;
    bool inNotes = false;
    QString notesText;

    auto flushShape = [&]() {
        if (inShape) {
            currentSlide.shapes.append(currentShape);
            currentShape = Shape{};
            inShape = false;
        }
    };
    auto flushParagraph = [&]() {
        if (inParagraph) {
            currentShape.paragraphs.append(currentParagraph);
            currentParagraph = TextParagraph{};
            inParagraph = false;
        }
    };
    auto flushSlide = [&]() {
        currentSlide.notes = notesText.trimmed();
        notesText.clear();
        presentation.slides.append(currentSlide);
        currentSlide = Slide{};
    };
    // AGENT-GUARD: shapes arrive both framed (our earlier files) and as
    // PAGE-LEVEL draw:rect / draw:ellipse / draw:custom-shape / draw:line
    // elements (conforming ODF, as LibreOffice writes). Only reading the
    // framed form made every .odp/.pptx import lose those shape kinds.
    auto beginShapeFromAttrs = [&](const QXmlStreamAttributes &attrs) {
        flushParagraph();
        flushShape();
        inShape = true;
        currentShape = Shape{};
        currentShape.id = attrs.value(QStringLiteral("draw:name")).toString();
        currentShape.rect =
            QRectF(lengthToPt(attrs.value(QStringLiteral("svg:x"))),
                   lengthToPt(attrs.value(QStringLiteral("svg:y"))),
                   lengthToPt(attrs.value(QStringLiteral("svg:width"))),
                   lengthToPt(attrs.value(QStringLiteral("svg:height"))));
        currentShape.buildStep =
            attrs.value(QStringLiteral("qq:build-step")).toInt();
        readShapeExtras(currentShape, attrs);
        const QString shapeTransform = attrs.value(QStringLiteral("draw:transform")).toString();
        if (shapeTransform.startsWith(QLatin1String("rotate ("))) {
            const double raw = shapeTransform.mid(8, shapeTransform.indexOf(QLatin1Char(')')) - 8)
                                   .toDouble();
            currentShape.rotation = qFabs(raw) > 2.0 * M_PI + 0.01
                ? raw // legacy degrees from the earlier release
                : qRadiansToDegrees(raw);
        }
        const QString shapeStyle = attrs.value(QStringLiteral("draw:style-name")).toString();
        if (styles.contains(shapeStyle)) {
            const GraphicStyle style = styles.value(shapeStyle);
            currentShape.fill = style.fill;
            currentShape.stroke = style.stroke;
            currentShape.strokeWidth = style.strokeWidth;
        }
    };

    while (!xml.atEnd()) {
        xml.readNext();
        const QStringView name = xml.name();
        const QStringView ns = xml.prefix();

        if (xml.isStartElement() && ns == QLatin1String("style")
            && name == QLatin1String("style")) {
            const QString styleName =
                xml.attributes().value(QStringLiteral("style:name")).toString();
            if (styleName.startsWith(QLatin1String("trans-"))) {
                if (xml.readNextStartElement() && xml.prefix() == QLatin1String("style")
                    && xml.name() == QLatin1String("drawing-page-properties")) {
                    const QString smil =
                        xml.attributes().value(QStringLiteral("smil:type")).toString();
                    pageTransitions.insert(styleName,
                                           smil.isEmpty() ? QStringLiteral("cut") : smil);
                }
            }
            if (styleName.startsWith(QLatin1String("shape-"))) {
                GraphicStyle style;
                if (xml.readNextStartElement() && xml.prefix() == QLatin1String("style")
                    && xml.name() == QLatin1String("graphic-properties")) {
                    const auto attrs = xml.attributes();
                    style.fill = colorFrom(attrs.value(QStringLiteral("draw:fill-color")));
                    style.stroke = colorFrom(attrs.value(QStringLiteral("svg:stroke-color")));
                    style.strokeWidth =
                        lengthToPt(attrs.value(QStringLiteral("svg:stroke-width")));
                    styles.insert(styleName, style);
                }
            }
        }

        if (xml.isStartElement() && ns == QLatin1String("style")
            && name == QLatin1String("page-layout")) {
            while (!(xml.isEndElement() && xml.prefix() == QLatin1String("style")
                     && xml.name() == QLatin1String("page-layout"))) {
                xml.readNext();
                if (xml.isStartElement() && xml.prefix() == QLatin1String("style")
                    && xml.name() == QLatin1String("page-layout-properties")) {
                    const auto attrs = xml.attributes();
                    presentation.slideSize =
                        QSizeF(lengthToPt(attrs.value(QStringLiteral("fo:page-width"))),
                               lengthToPt(attrs.value(QStringLiteral("fo:page-height"))));
                }
            }
        }

        if (xml.isStartElement() && ns == QLatin1String("style")
            && name == QLatin1String("master-page")) {
            const auto attrs = xml.attributes();
            const QString id = attrs.value(QStringLiteral("style:name")).toString();
            // "Default" is the placeholder every fodp carries; it is not one
            // of the deck's own masters.
            if (!id.isEmpty() && id != QLatin1String("Default")
                && attrs.hasAttribute(QStringLiteral("qq:background"))) {
                SlideMaster master;
                master.id = id;
                master.name = attrs.value(QStringLiteral("qq:master-name")).toString();
                master.background =
                    QColor(attrs.value(QStringLiteral("qq:background")).toString());
                master.titleColor =
                    QColor(attrs.value(QStringLiteral("qq:title-color")).toString());
                master.bodyColor =
                    QColor(attrs.value(QStringLiteral("qq:body-color")).toString());
                master.fontFamily = attrs.value(QStringLiteral("qq:font")).toString();
                presentation.masters.append(master);
            }
            continue;
        }

        if (xml.isStartElement() && ns == QLatin1String("draw") && name == QLatin1String("page")) {
            inSlide = true;
            currentSlide = Slide{};
            const auto attrs = xml.attributes();
            currentSlide.id = attrs.value(QStringLiteral("draw:name")).toString();
            currentSlide.title = attrs.value(QStringLiteral("qq:title")).toString();
            currentSlide.layout = attrs.value(QStringLiteral("qq:layout")).toString();
            const QString masterName =
                attrs.value(QStringLiteral("draw:master-page-name")).toString();
            // "Default" is the ODF placeholder the writer always emits for a
            // slide with no master of its own.
            currentSlide.master =
                masterName == QLatin1String("Default") ? QString() : masterName;
            const QString styleName = attrs.value(QStringLiteral("draw:style-name")).toString();
            if (styleName.startsWith(QLatin1String("trans-"))) {
                currentSlide.transition = pageTransitions.value(styleName, QStringLiteral("cut"));
            }
            continue;
        }
        if (xml.isEndElement() && ns == QLatin1String("draw") && name == QLatin1String("page")) {
            flushShape();
            flushSlide();
            inSlide = false;
            continue;
        }

        if (inSlide && xml.isStartElement() && ns == QLatin1String("draw")
            && (name == QLatin1String("rect") || name == QLatin1String("ellipse"))) {
            beginShapeFromAttrs(xml.attributes());
            currentShape.kind = name == QLatin1String("rect") ? ShapeKind::Rectangle
                                                              : ShapeKind::Ellipse;
            if (xml.isEndElement()) {
                flushShape(); // self-closing element has no further tokens
            }
            continue;
        }
        if (inSlide && xml.isStartElement() && ns == QLatin1String("draw")
            && name == QLatin1String("custom-shape") && !inShape) {
            beginShapeFromAttrs(xml.attributes());
            // Kind resolves through enhanced-geometry below; self-closing
            // (empty) shapes flush immediately.
            if (xml.isEndElement()) {
                flushShape();
            }
            continue;
        }
        if (inSlide && xml.isStartElement() && ns == QLatin1String("draw")
            && name == QLatin1String("line") && !inShape) {
            beginShapeFromAttrs(xml.attributes());
            const auto attrs = xml.attributes();
            currentShape.kind = attrs.hasAttribute(QStringLiteral("draw:marker-end"))
                ? ShapeKind::Arrow
                : ShapeKind::Line;
            // draw:line endpoints are absolute page coordinates.
            const QPointF from(lengthToPt(attrs.value(QStringLiteral("svg:x1"))),
                               lengthToPt(attrs.value(QStringLiteral("svg:y1"))));
            const QPointF to(lengthToPt(attrs.value(QStringLiteral("svg:x2"))),
                             lengthToPt(attrs.value(QStringLiteral("svg:y2"))));
            currentShape.rect = QRectF(from, to).normalized();
            flushShape();
            continue;
        }
        if (inSlide && xml.isEndElement() && ns == QLatin1String("draw")
            && (name == QLatin1String("custom-shape") || name == QLatin1String("rect")
                || name == QLatin1String("ellipse"))) {
            flushParagraph();
            flushShape();
            continue;
        }
        if (inSlide && xml.isStartElement() && ns == QLatin1String("draw")
            && name == QLatin1String("frame")) {
            const auto attrs = xml.attributes();
            const QString styleName = attrs.value(QStringLiteral("draw:style-name")).toString();
            if (attrs.value(QStringLiteral("presentation:class"))
                == QLatin1String("notes")) {
                inNotes = true;
                continue;
            }
            flushShape();
            inShape = true;
            currentShape = Shape{};
            currentShape.id = attrs.value(QStringLiteral("draw:name")).toString();
            currentShape.rect =
                QRectF(lengthToPt(attrs.value(QStringLiteral("svg:x"))),
                       lengthToPt(attrs.value(QStringLiteral("svg:y"))),
                       lengthToPt(attrs.value(QStringLiteral("svg:width"))),
                       lengthToPt(attrs.value(QStringLiteral("svg:height"))));
            currentShape.buildStep =
                attrs.value(QStringLiteral("qq:build-step")).toInt();
            readShapeExtras(currentShape, attrs);
            const QString transform = attrs.value(QStringLiteral("draw:transform")).toString();
            if (transform.startsWith(QLatin1String("rotate ("))) {
                // ODF rotate() is radians; files written by the earlier
                // degrees-based release carry values in the ±360 range while
                // conforming radians stay within ±2π — accept the legacy
                // interpretation only for those out-of-range values.
                const double raw =
                    transform.mid(8, transform.indexOf(QLatin1Char(')')) - 8).toDouble();
                currentShape.rotation = qFabs(raw) > 2.0 * M_PI + 0.01
                    ? raw // legacy degrees
                    : qRadiansToDegrees(raw);
            }
            if (styles.contains(styleName)) {
                const GraphicStyle style = styles.value(styleName);
                currentShape.fill = style.fill;
                currentShape.stroke = style.stroke;
                currentShape.strokeWidth = style.strokeWidth;
            }
            continue;
        }
        if (inShape && xml.isEndElement() && ns == QLatin1String("draw")
            && name == QLatin1String("frame")) {
            flushParagraph();
            flushShape();
            continue;
        }

        if (inShape && xml.isStartElement() && ns == QLatin1String("office")
            && name == QLatin1String("binary-data")) {
            xml.readNext();
            currentShape.imagePng = QByteArray::fromBase64(xml.text().toString().toLatin1());
            continue;
        }
        if (inShape && xml.isStartElement()) {
            if (ns == QLatin1String("draw") && name == QLatin1String("text-box")) {
                // A text-box child only DEFINES a text-box shape; inside a
                // resolved custom shape it is just that shape's text host.
                if (currentShape.kind == ShapeKind::TextBox
                    || currentShape.kind == ShapeKind::Image) {
                    currentShape.kind = ShapeKind::TextBox;
                }
            } else if (ns == QLatin1String("draw") && name == QLatin1String("custom-shape")) {
                // geometry type resolves when enhanced-geometry appears
            } else if (ns == QLatin1String("draw") && name == QLatin1String("enhanced-geometry")) {
                const QString type =
                    xml.attributes().value(QStringLiteral("draw:type")).toString();
                currentShape.kind = type == QLatin1String("ellipse") ? ShapeKind::Ellipse
                                                                     : ShapeKind::Rectangle;
            } else if (ns == QLatin1String("draw") && name == QLatin1String("line")) {
                currentShape.kind = ShapeKind::Line;
                const auto attrs = xml.attributes();
                if (attrs.hasAttribute(QStringLiteral("draw:marker-end"))) {
                    currentShape.kind = ShapeKind::Arrow;
                }
            } else if (ns == QLatin1String("draw") && name == QLatin1String("image")) {
                currentShape.kind = ShapeKind::Image;
            } else if (ns == QLatin1String("text") && name == QLatin1String("p")) {
                inParagraph = true;
                currentParagraph = TextParagraph{};
                const QString align =
                    xml.attributes().value(QStringLiteral("fo:text-align")).toString();
                if (align == QLatin1String("center")) {
                    currentParagraph.alignment = int(Qt::AlignHCenter);
                } else if (align == QLatin1String("right")) {
                    currentParagraph.alignment = int(Qt::AlignRight);
                }
                const QString styleName =
                    xml.attributes().value(QStringLiteral("text:style-name")).toString();
                if (styleName.startsWith(QLatin1String("qq-list-"))) {
                    currentParagraph.bullet = true;
                    currentParagraph.listLevel = styleName.mid(8).toInt() - 1;
                }
            } else if (ns == QLatin1String("text") && name == QLatin1String("span")) {
                // AGENT-GUARD: snapshot the attributes BEFORE readNext().
                // QXmlStreamReader::attributes() is only valid on start
                // elements; after readNext() it returns an empty set, which
                // silently stripped every text run's formatting on load
                // (and the next save baked the loss in).
                const QXmlStreamAttributes spanAttrs = xml.attributes();
                xml.readNext(); // text node
                currentParagraph.runs.append(readRun(spanAttrs, xml.text().toString()));
            }
            continue;
        }
        if (inParagraph && xml.isEndElement() && ns == QLatin1String("text")
            && name == QLatin1String("p")) {
            flushParagraph();
            continue;
        }
        if (inNotes && xml.isStartElement() && ns == QLatin1String("text")
            && name == QLatin1String("p")) {
            // Multi-paragraph notes need a separator — concatenation ran
            // LibreOffice paragraphs together on import.
            if (!notesText.isEmpty()) {
                notesText += QLatin1Char('\n');
            }
            xml.readNext();
            notesText += xml.text().toString();
            continue;
        }
        if (inNotes && xml.isEndElement() && ns == QLatin1String("presentation")
            && name == QLatin1String("notes")) {
            inNotes = false;
            continue;
        }
    }

    if (xml.hasError() && error != nullptr) {
        *error = xml.errorString();
    }
    return presentation;
}

} // namespace QindaOffice::Show
