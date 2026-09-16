// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_writer.h"

#include <QBuffer>
#include <QXmlStreamWriter>

namespace QindaOffice::Show {

namespace {

// ODF body coordinates are cm (page layout) while shapes are pt; the
// writer converts.
qreal ptToCm(qreal pt)
{
    return pt * 2.54 / 72.0;
}

QString shapeGeometry(ShapeKind kind)
{
    switch (kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::TextBox:
        return QStringLiteral("rectangle");
    case ShapeKind::Ellipse:
        return QStringLiteral("ellipse");
    case ShapeKind::Line:
    case ShapeKind::Arrow:
    case ShapeKind::Image:
        return {};
    }
    return {};
}

void writeText(QXmlStreamWriter &xml, const QList<TextParagraph> &paragraphs)
{
    for (const TextParagraph &paragraph : paragraphs) {
        xml.writeStartElement(QStringLiteral("text:p"));
        if (paragraph.bullet) {
            xml.writeAttribute(QStringLiteral("text:style-name"),
                               QStringLiteral("qq-list-%1").arg(paragraph.listLevel + 1));
        }
        QString alignment;
        if (paragraph.alignment & int(Qt::AlignHCenter)) {
            alignment = QStringLiteral("center");
        } else if (paragraph.alignment & int(Qt::AlignRight)) {
            alignment = QStringLiteral("right");
        }
        if (!alignment.isEmpty()) {
            xml.writeAttribute(QStringLiteral("fo:text-align"), alignment);
        }
        for (const TextRun &run : paragraph.runs) {
            xml.writeStartElement(QStringLiteral("text:span"));
            if (run.bold) {
                xml.writeAttribute(QStringLiteral("fo:font-weight"), QStringLiteral("bold"));
            }
            if (run.italic) {
                xml.writeAttribute(QStringLiteral("fo:font-style"), QStringLiteral("italic"));
            }
            if (run.color.isValid() && run.color != QColor(Qt::black)) {
                xml.writeAttribute(QStringLiteral("fo:color"), run.color.name());
            }
            if (run.pointSize > 0) {
                xml.writeAttribute(QStringLiteral("fo:font-size"),
                                   QStringLiteral("%1pt").arg(run.pointSize));
            }
            if (!run.fontFamily.isEmpty()) {
                xml.writeAttribute(QStringLiteral("style:font-name"), run.fontFamily);
            }
            xml.writeCharacters(run.text);
            xml.writeEndElement(); // text:span
        }
        xml.writeEndElement(); // text:p
    }
}

// AGENT-GUARD: ODF only allows draw:image / draw:text-box / draw:object ...
// INSIDE draw:frame. draw:line and draw:custom-shape are PAGE-LEVEL shapes —
// writing them inside a frame is invalid ODF and LibreOffice silently drops
// them both ways (they were written framed here and only read framed, so
// .odp/.pptx conversion lost every rect/ellipse/line/arrow).
void writeShape(QXmlStreamWriter &xml, const Shape &shape)
{
    const QString transform =
        shape.rotation != 0
            ? QStringLiteral("rotate (%1)").arg(qDegreesToRadians(shape.rotation), 0, 'g', 10)
            : QString();
    const QString buildStep = shape.buildStep > 0
        ? QString::number(shape.buildStep)
        : QString();

    if (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow) {
        // Page-level draw:line with absolute endpoint coordinates.
        xml.writeStartElement(QStringLiteral("draw:line"));
        xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
        if (!transform.isEmpty()) {
            xml.writeAttribute(QStringLiteral("draw:transform"), transform);
        }
        if (!buildStep.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:build-step"), buildStep);
        }
        xml.writeAttribute(QStringLiteral("svg:x1"),
                           QString::number(ptToCm(shape.rect.x()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:y1"),
                           QString::number(ptToCm(shape.rect.y()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:x2"),
                           QString::number(ptToCm(shape.rect.right()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:y2"),
                           QString::number(ptToCm(shape.rect.bottom()), 'f', 5));
        if (shape.kind == ShapeKind::Arrow) {
            xml.writeAttribute(QStringLiteral("draw:marker-end"), QStringLiteral("Arrow"));
        }
        xml.writeEndElement(); // draw:line
        return;
    }

    if (shape.kind == ShapeKind::Rectangle || shape.kind == ShapeKind::Ellipse) {
        // Page-level draw:custom-shape (svg:viewBox mirrors the geometry in
        // 1/100 mm, matching LibreOffice's convention).
        xml.writeStartElement(QStringLiteral("draw:custom-shape"));
        xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
        xml.writeAttribute(QStringLiteral("svg:x"), QString::number(ptToCm(shape.rect.x()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:y"), QString::number(ptToCm(shape.rect.y()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:width"),
                           QString::number(ptToCm(shape.rect.width()), 'f', 5));
        xml.writeAttribute(QStringLiteral("svg:height"),
                           QString::number(ptToCm(shape.rect.height()), 'f', 5));
        if (!transform.isEmpty()) {
            xml.writeAttribute(QStringLiteral("draw:transform"), transform);
        }
        if (!buildStep.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:build-step"), buildStep);
        }
        xml.writeStartElement(QStringLiteral("draw:enhanced-geometry"));
        xml.writeAttribute(QStringLiteral("draw:type"), shapeGeometry(shape.kind));
        xml.writeAttribute(QStringLiteral("svg:viewBox"),
                           QStringLiteral("0 0 %1 %2")
                               .arg(qRound(ptToCm(shape.rect.width()) * 1000.0))
                               .arg(qRound(ptToCm(shape.rect.height()) * 1000.0)));
        xml.writeEndElement(); // enhanced-geometry
        writeText(xml, shape.paragraphs);
        xml.writeEndElement(); // draw:custom-shape
        return;
    }

    // Images and text boxes keep draw:frame (conforming container).
    xml.writeStartElement(QStringLiteral("draw:frame"));
    xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
    xml.writeAttribute(QStringLiteral("svg:x"), QString::number(ptToCm(shape.rect.x()), 'f', 5));
    xml.writeAttribute(QStringLiteral("svg:y"), QString::number(ptToCm(shape.rect.y()), 'f', 5));
    xml.writeAttribute(QStringLiteral("svg:width"),
                       QString::number(ptToCm(shape.rect.width()), 'f', 5));
    xml.writeAttribute(QStringLiteral("svg:height"),
                       QString::number(ptToCm(shape.rect.height()), 'f', 5));
    if (!transform.isEmpty()) {
        xml.writeAttribute(QStringLiteral("draw:transform"), transform);
    }
    if (!buildStep.isEmpty()) {
        xml.writeAttribute(QStringLiteral("qq:build-step"), buildStep);
    }
    if (shape.kind == ShapeKind::Image) {
        xml.writeStartElement(QStringLiteral("draw:image"));
        xml.writeTextElement(QStringLiteral("office:binary-data"),
                             QString::fromLatin1(shape.imagePng.toBase64()));
        xml.writeEndElement(); // draw:image
    } else { // TextBox
        xml.writeStartElement(QStringLiteral("draw:text-box"));
        writeText(xml, shape.paragraphs);
        xml.writeEndElement(); // draw:text-box
    }
    xml.writeEndElement(); // draw:frame
}

} // namespace

QByteArray writeFodp(const Presentation &presentation, QString *error)
{
    Q_UNUSED(error);
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.writeNamespace(QStringLiteral("http://www.w3.org/2001/XInclude"), QStringLiteral("xi"));

    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("office:document"));
    xml.writeAttribute(QStringLiteral("office:mimetype"),
                       QStringLiteral("application/vnd.oasis.opendocument.presentation"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0"),
                       QStringLiteral("office"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0"),
                       QStringLiteral("style"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0"),
                       QStringLiteral("text"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"),
                       QStringLiteral("draw"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"),
                       QStringLiteral("svg"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"),
                       QStringLiteral("fo"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"),
                       QStringLiteral("presentation"));
    xml.writeNamespace(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0"),
                       QStringLiteral("smil"));
    xml.writeNamespace(QStringLiteral("https://qindaqt.example.org/qqo"), QStringLiteral("qq"));

    // Automatic styles: one graphic style per shape + the page layout.
    xml.writeStartElement(QStringLiteral("office:automatic-styles"));
    xml.writeStartElement(QStringLiteral("style:page-layout"));
    xml.writeAttribute(QStringLiteral("style:name"), QStringLiteral("Default"));
    xml.writeStartElement(QStringLiteral("style:page-layout-properties"));
    xml.writeAttribute(QStringLiteral("fo:page-width"),
                       QStringLiteral("%1cm").arg(ptToCm(presentation.slideSize.width()), 0, 'f', 5));
    xml.writeAttribute(QStringLiteral("fo:page-height"),
                       QStringLiteral("%1cm").arg(ptToCm(presentation.slideSize.height()), 0, 'f', 5));
    xml.writeEndElement(); // properties
    xml.writeEndElement(); // page-layout

    for (const Slide &slide : presentation.slides) {
        xml.writeStartElement(QStringLiteral("style:style"));
        xml.writeAttribute(QStringLiteral("style:name"),
                           QStringLiteral("trans-%1").arg(slide.id));
        xml.writeAttribute(QStringLiteral("style:family"), QStringLiteral("drawing-page"));
        xml.writeEmptyElement(QStringLiteral("style:drawing-page-properties"));
        if (slide.transition != QLatin1String("cut")) {
            xml.writeAttribute(QStringLiteral("smil:type"), slide.transition);
        }
        xml.writeEndElement(); // style
        for (const Shape &shape : slide.shapes) {
            xml.writeStartElement(QStringLiteral("style:style"));
            xml.writeAttribute(QStringLiteral("style:name"),
                               QStringLiteral("shape-%1").arg(shape.id));
            xml.writeAttribute(QStringLiteral("style:family"), QStringLiteral("graphic"));
            xml.writeStartElement(QStringLiteral("style:graphic-properties"));
            if (shape.fill.isValid() && shape.fill.alpha() > 0) {
                xml.writeAttribute(QStringLiteral("draw:fill"), QStringLiteral("solid"));
                xml.writeAttribute(QStringLiteral("draw:fill-color"), shape.fill.name());
            } else {
                xml.writeAttribute(QStringLiteral("draw:fill"), QStringLiteral("none"));
            }
            if (shape.strokeWidth > 0) {
                xml.writeAttribute(QStringLiteral("draw:stroke"), QStringLiteral("solid"));
                xml.writeAttribute(QStringLiteral("svg:stroke-color"), shape.stroke.name());
                xml.writeAttribute(QStringLiteral("svg:stroke-width"),
                                   QStringLiteral("%1pt").arg(shape.strokeWidth));
            } else {
                xml.writeAttribute(QStringLiteral("draw:stroke"), QStringLiteral("none"));
            }
            xml.writeEndElement(); // graphic-properties
            xml.writeEndElement(); // style
        }
    }
    xml.writeEndElement(); // automatic-styles

    xml.writeStartElement(QStringLiteral("office:master-styles"));
    xml.writeStartElement(QStringLiteral("style:master-page"));
    xml.writeAttribute(QStringLiteral("style:name"), QStringLiteral("Default"));
    xml.writeAttribute(QStringLiteral("style:page-layout-name"), QStringLiteral("Default"));
    xml.writeEndElement(); // master-page
    xml.writeEndElement(); // master-styles

    xml.writeStartElement(QStringLiteral("office:body"));
    xml.writeStartElement(QStringLiteral("office:presentation"));
    for (const Slide &slide : presentation.slides) {
        xml.writeStartElement(QStringLiteral("draw:page"));
        xml.writeAttribute(QStringLiteral("draw:name"), slide.id);
        xml.writeAttribute(QStringLiteral("draw:master-page-name"), QStringLiteral("Default"));
        xml.writeAttribute(QStringLiteral("draw:style-name"),
                           QStringLiteral("trans-%1").arg(slide.id));
        // qq: extension namespace for app metadata (title, layout, notes
        // already have ODF homes; layout rides qq in v1).
        xml.writeAttribute(QStringLiteral("qq:title"), slide.title);
        xml.writeAttribute(QStringLiteral("qq:layout"), slide.layout);

        for (const Shape &shape : slide.shapes) {
            writeShape(xml, shape);
        }

        if (!slide.notes.isEmpty()) {
            xml.writeStartElement(QStringLiteral("presentation:notes"));
            xml.writeStartElement(QStringLiteral("draw:frame"));
            xml.writeAttribute(QStringLiteral("presentation:class"), QStringLiteral("notes"));
            xml.writeAttribute(QStringLiteral("svg:x"), QStringLiteral("0cm"));
            xml.writeAttribute(QStringLiteral("svg:y"), QStringLiteral("0cm"));
            xml.writeAttribute(QStringLiteral("svg:width"), QStringLiteral("1cm"));
            xml.writeAttribute(QStringLiteral("svg:height"), QStringLiteral("1cm"));
            xml.writeStartElement(QStringLiteral("draw:text-box"));
            xml.writeTextElement(QStringLiteral("text:p"), slide.notes);
            xml.writeEndElement(); // text-box
            xml.writeEndElement(); // frame
            xml.writeEndElement(); // presentation:notes
        }
        xml.writeEndElement(); // draw:page
    }
    xml.writeEndElement(); // office:presentation
    xml.writeEndElement(); // office:body
    xml.writeEndElement(); // office:document
    xml.writeEndDocument();
    return out;
}

} // namespace QindaOffice::Show
