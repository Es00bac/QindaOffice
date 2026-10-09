// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_writer.h"

#include "fodp_extended.h"
#include "fodp_styles.h"
#include "odf_dom.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QXmlStreamWriter>

namespace QindaOffice::Show {

namespace {

using Fodp::StyleRegistry;

QString number(qreal value)
{
    return QString::number(value, 'f', QLocale::FloatingPointShortest);
}

QString rectString(const QRectF &rect)
{
    return QStringLiteral("%1 %2 %3 %4")
        .arg(number(rect.x()), number(rect.y()), number(rect.width()), number(rect.height()));
}

// The qq: attributes ODF has no home for: builds, animation, grouping, the
// exact rotation and the extension payload. Paired with readShapeExtras in
// fodp_reader.cpp — a field one side skips is a field the user loses.
void writeShapeExtras(QXmlStreamWriter &xml, const Shape &shape)
{
    if (shape.buildStep > 0) {
        xml.writeAttribute(QStringLiteral("qq:build-step"), QString::number(shape.buildStep));
    }
    if (shape.animation != ShapeAnimation::Appear) {
        xml.writeAttribute(QStringLiteral("qq:animation"), QString::number(int(shape.animation)));
    }
    if (shape.animation != ShapeAnimation::Appear || shape.animationMs != Shape().animationMs) {
        xml.writeAttribute(QStringLiteral("qq:animation-ms"), QString::number(shape.animationMs));
    }
    if (!shape.groupId.isEmpty()) {
        xml.writeAttribute(QStringLiteral("qq:group"), shape.groupId);
    }
    if (shape.rotation != 0) {
        xml.writeAttribute(QStringLiteral("qq:rotation"), number(shape.rotation));
    }
    if (!shape.extra.isEmpty()) {
        xml.writeAttribute(QStringLiteral("qq:extra"),
                           QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(shape.extra))
                                                 .toJson(QJsonDocument::Compact)));
    }
    // M62: the exact shape when the ODF spelling cannot carry it all.
    if (Fodp::needsExtendedCopy(shape)) {
        xml.writeAttribute(QStringLiteral("qq:shape"), Fodp::extendedCopy(shape));
    }
}

// svg:x/y/width/height for an unrotated shape; width/height plus the ODF
// rotate+translate pair for a rotated one (svg:x/y must then be absent).
void writeFrameGeometry(QXmlStreamWriter &xml, const Shape &shape)
{
    const QRectF rect = shape.rect.normalized();
    if (shape.rotation == 0) {
        xml.writeAttribute(QStringLiteral("svg:x"), Odf::ptToLength(rect.x()));
        xml.writeAttribute(QStringLiteral("svg:y"), Odf::ptToLength(rect.y()));
    }
    xml.writeAttribute(QStringLiteral("svg:width"), Odf::ptToLength(rect.width()));
    xml.writeAttribute(QStringLiteral("svg:height"), Odf::ptToLength(rect.height()));
    if (shape.rotation != 0) {
        xml.writeAttribute(QStringLiteral("draw:transform"), Odf::rotationTransform(rect, shape.rotation));
    }
    // The model's exact rect when ODF's spelling cannot carry it bit for
    // bit (a rotated shape round-trips through sin/cos).
    if (rect != shape.rect || shape.rotation != 0) {
        xml.writeAttribute(QStringLiteral("qq:rect"), rectString(shape.rect));
    }
}

void writeLine(QXmlStreamWriter &xml, StyleRegistry &styles, const Shape &shape)
{
    // AGENT-NOTE: a line runs from rect.topLeft() to rect.bottomRight()
    // with the rect UN-normalised, so its direction (where the arrowhead
    // goes) is the sign of width/height. Rotation is baked into the
    // endpoints LibreOffice sees; qq:rect + qq:rotation keep the model's
    // exact pair.
    QPointF from = shape.rect.topLeft();
    QPointF to = shape.rect.bottomRight();
    if (shape.rotation != 0) {
        QTransform turn;
        turn.translate(shape.rect.center().x(), shape.rect.center().y());
        turn.rotate(shape.rotation);
        turn.translate(-shape.rect.center().x(), -shape.rect.center().y());
        from = turn.map(from);
        to = turn.map(to);
    }
    xml.writeStartElement(QStringLiteral("draw:line"));
    xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
    xml.writeAttribute(QStringLiteral("draw:style-name"), styles.graphicStyle(shape));
    xml.writeAttribute(QStringLiteral("svg:x1"), Odf::ptToLength(from.x()));
    xml.writeAttribute(QStringLiteral("svg:y1"), Odf::ptToLength(from.y()));
    xml.writeAttribute(QStringLiteral("svg:x2"), Odf::ptToLength(to.x()));
    xml.writeAttribute(QStringLiteral("svg:y2"), Odf::ptToLength(to.y()));
    if (shape.rotation != 0) {
        xml.writeAttribute(QStringLiteral("qq:rect"), rectString(shape.rect));
    }
    writeShapeExtras(xml, shape);
    xml.writeEndElement(); // draw:line
}

// AGENT-GUARD: ODF only allows draw:image / draw:text-box / draw:object …
// INSIDE draw:frame. draw:line and draw:custom-shape are PAGE-LEVEL shapes;
// framing them is invalid and LibreOffice drops them both ways.
void writeShape(QXmlStreamWriter &xml, StyleRegistry &styles, const Shape &shape)
{
    if (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow) {
        writeLine(xml, styles, shape);
        return;
    }
    if (shape.kind == ShapeKind::Table || shape.kind == ShapeKind::Chart || shape.kind == ShapeKind::Group
        || shape.kind == ShapeKind::Preserved) {
        // LibreOffice sees a picture of what QindaShow draws; qq:shape keeps
        // the shape itself.
        xml.writeStartElement(QStringLiteral("draw:frame"));
        xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
        xml.writeAttribute(QStringLiteral("draw:style-name"), styles.graphicStyle(shape));
        writeFrameGeometry(xml, shape);
        writeShapeExtras(xml, shape);
        xml.writeStartElement(QStringLiteral("draw:image"));
        xml.writeTextElement(QStringLiteral("office:binary-data"), QString::fromLatin1(Fodp::shapePicture(shape).toBase64()));
        xml.writeEndElement(); // draw:image
        xml.writeEndElement(); // draw:frame
        return;
    }
    if (shape.kind == ShapeKind::Rectangle || shape.kind == ShapeKind::Ellipse || shape.kind == ShapeKind::AutoShape) {
        xml.writeStartElement(QStringLiteral("draw:custom-shape"));
        xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
        xml.writeAttribute(QStringLiteral("draw:style-name"), styles.graphicStyle(shape));
        writeFrameGeometry(xml, shape);
        writeShapeExtras(xml, shape);
        // Schema order: the text first, then the geometry.
        Fodp::writeParagraphs(xml, styles, shape.paragraphs);
        xml.writeEmptyElement(QStringLiteral("draw:enhanced-geometry"));
        xml.writeAttribute(QStringLiteral("svg:viewBox"), QStringLiteral("0 0 21600 21600"));
        xml.writeAttribute(QStringLiteral("draw:type"), Fodp::odfShapeType(shape));
        xml.writeEndElement(); // draw:custom-shape
        return;
    }
    xml.writeStartElement(QStringLiteral("draw:frame"));
    xml.writeAttribute(QStringLiteral("draw:name"), shape.id);
    xml.writeAttribute(QStringLiteral("draw:style-name"), styles.graphicStyle(shape));
    writeFrameGeometry(xml, shape);
    writeShapeExtras(xml, shape);
    if (shape.kind == ShapeKind::Image) {
        xml.writeStartElement(QStringLiteral("draw:image"));
        xml.writeTextElement(QStringLiteral("office:binary-data"), QString::fromLatin1(shape.imagePng.toBase64()));
        xml.writeEndElement(); // draw:image
    } else {
        xml.writeStartElement(QStringLiteral("draw:text-box"));
        Fodp::writeParagraphs(xml, styles, shape.paragraphs);
        xml.writeEndElement(); // draw:text-box
    }
    xml.writeEndElement(); // draw:frame
}

void writeNotes(QXmlStreamWriter &xml, const QString &notes, int pageNumber)
{
    // A portrait notes page: the slide thumbnail on top, the notes below.
    xml.writeStartElement(QStringLiteral("presentation:notes"));
    xml.writeEmptyElement(QStringLiteral("draw:page-thumbnail"));
    xml.writeAttribute(QStringLiteral("svg:x"), Odf::ptToLength(42));
    xml.writeAttribute(QStringLiteral("svg:y"), Odf::ptToLength(60));
    xml.writeAttribute(QStringLiteral("svg:width"), Odf::ptToLength(528));
    xml.writeAttribute(QStringLiteral("svg:height"), Odf::ptToLength(297));
    xml.writeAttribute(QStringLiteral("draw:page-number"), QString::number(pageNumber));
    xml.writeAttribute(QStringLiteral("presentation:class"), QStringLiteral("page"));
    xml.writeStartElement(QStringLiteral("draw:frame"));
    xml.writeAttribute(QStringLiteral("presentation:class"), QStringLiteral("notes"));
    xml.writeAttribute(QStringLiteral("svg:x"), Odf::ptToLength(61));
    xml.writeAttribute(QStringLiteral("svg:y"), Odf::ptToLength(376));
    xml.writeAttribute(QStringLiteral("svg:width"), Odf::ptToLength(490));
    xml.writeAttribute(QStringLiteral("svg:height"), Odf::ptToLength(356));
    xml.writeStartElement(QStringLiteral("draw:text-box"));
    // One text:p per line: a newline inside a paragraph is white space to
    // ODF, so the old single paragraph ran the lines together.
    for (const QString &line : notes.split(QLatin1Char('\n'))) {
        xml.writeStartElement(QStringLiteral("text:p"));
        const bool formatting = xml.autoFormatting();
        xml.setAutoFormatting(false);
        bool previousSpace = true;
        Fodp::writeOdfText(xml, line, &previousSpace, true);
        xml.writeEndElement(); // text:p
        xml.setAutoFormatting(formatting);
    }
    xml.writeEndElement(); // text-box
    xml.writeEndElement(); // frame
    xml.writeEndElement(); // presentation:notes
}

QString masterPageName(const Presentation &presentation, const Slide &slide)
{
    for (qsizetype i = 0; i < presentation.masters.size(); ++i) {
        if (presentation.masters.at(i).id == slide.master) {
            return QStringLiteral("M%1").arg(i + 1);
        }
    }
    return QStringLiteral("Default");
}

void writeBody(QXmlStreamWriter &xml, StyleRegistry &styles, const Presentation &presentation)
{
    xml.writeStartElement(QStringLiteral("office:body"));
    xml.writeStartElement(QStringLiteral("office:presentation"));
    if (!presentation.layouts.isEmpty()) {
        xml.writeAttribute(QStringLiteral("qq:layouts"), Fodp::layoutsJson(presentation.layouts));
    }
    if (presentation.notesSize != Presentation().notesSize) {
        xml.writeAttribute(QStringLiteral("qq:notes-size"), QStringLiteral("%1 %2")
                                                                .arg(number(presentation.notesSize.width()),
                                                                     number(presentation.notesSize.height())));
    }
    // M75 (D-084): sections and the comment kind ride qq: too.
    if (!presentation.sections.isEmpty()) {
        xml.writeAttribute(QStringLiteral("qq:sections"), Fodp::sectionsJson(presentation.sections));
    }
    if (presentation.legacyComments) {
        xml.writeAttribute(QStringLiteral("qq:legacy-comments"), QStringLiteral("1"));
    }
    for (qsizetype i = 0; i < presentation.slides.size(); ++i) {
        const Slide &slide = presentation.slides.at(i);
        xml.writeStartElement(QStringLiteral("draw:page"));
        xml.writeAttribute(QStringLiteral("draw:name"), slide.id);
        xml.writeAttribute(QStringLiteral("draw:style-name"), styles.pageStyle(slide));
        xml.writeAttribute(QStringLiteral("draw:master-page-name"), masterPageName(presentation, slide));
        xml.writeAttribute(QStringLiteral("qq:title"), slide.title);
        xml.writeAttribute(QStringLiteral("qq:layout"), slide.layout);
        if (!slide.master.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:master"), slide.master);
        }
        if (!slide.showMasterShapes) {
            xml.writeAttribute(QStringLiteral("qq:show-master"), QStringLiteral("0"));
        }
        if (slide.background.set) {
            xml.writeAttribute(QStringLiteral("qq:background"), Fodp::backgroundJson(slide.background));
        }
        if (!slide.layoutId.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:layout-id"), slide.layoutId);
        }
        if (!slide.comments.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:comments"), Fodp::commentsJson(slide.comments));
        }
        for (const Shape &shape : slide.shapes) {
            writeShape(xml, styles, shape);
        }
        if (!slide.notes.isEmpty()) {
            writeNotes(xml, slide.notes, int(i) + 1);
        }
        xml.writeEndElement(); // draw:page
    }
    xml.writeEndElement(); // office:presentation
    xml.writeEndElement(); // office:body
}

// A master's decorations (logos, rules) as pictures on its ODF master page,
// for LibreOffice; QindaShow reads them back from qq:shapes.
void writeMasterDecorations(QXmlStreamWriter &xml, StyleRegistry &styles, const SlideMaster &master)
{
    for (const Shape &shape : master.shapes) {
        Shape picture;
        picture.id = shape.id;
        picture.kind = ShapeKind::Image;
        picture.rect = shape.rect.normalized();
        picture.rotation = shape.rotation;
        picture.imagePng = Fodp::shapePicture(shape);
        if (!picture.imagePng.isEmpty()) {
            writeShape(xml, styles, picture);
        }
    }
}

void writeMasterStyles(QXmlStreamWriter &xml, StyleRegistry &styles, const Presentation &presentation)
{
    xml.writeStartElement(QStringLiteral("office:master-styles"));
    xml.writeEmptyElement(QStringLiteral("style:master-page"));
    xml.writeAttribute(QStringLiteral("style:name"), QStringLiteral("Default"));
    xml.writeAttribute(QStringLiteral("style:page-layout-name"), QString::fromUtf16(StyleRegistry::kPageLayout));
    // The deck's own masters: ODF master pages carry the background; the
    // type defaults and the model id ride qq: (style names must be NCNames,
    // and an id may start with a digit).
    for (qsizetype i = 0; i < presentation.masters.size(); ++i) {
        const SlideMaster &master = presentation.masters.at(i);
        xml.writeStartElement(QStringLiteral("style:master-page"));
        xml.writeAttribute(QStringLiteral("style:name"), QStringLiteral("M%1").arg(i + 1));
        if (!master.name.isEmpty()) {
            xml.writeAttribute(QStringLiteral("style:display-name"), master.name);
        }
        xml.writeAttribute(QStringLiteral("style:page-layout-name"), QString::fromUtf16(StyleRegistry::kPageLayout));
        xml.writeAttribute(QStringLiteral("draw:style-name"), styles.masterPageStyle(master.background));
        xml.writeAttribute(QStringLiteral("qq:id"), master.id);
        xml.writeAttribute(QStringLiteral("qq:master-name"), master.name);
        xml.writeAttribute(QStringLiteral("qq:background"), master.background.name(QColor::HexArgb));
        xml.writeAttribute(QStringLiteral("qq:title-color"), master.titleColor.name(QColor::HexArgb));
        xml.writeAttribute(QStringLiteral("qq:body-color"), master.bodyColor.name(QColor::HexArgb));
        xml.writeAttribute(QStringLiteral("qq:font"), master.fontFamily);
        if (!master.typeDefaults) {
            xml.writeAttribute(QStringLiteral("qq:type-defaults"), QStringLiteral("0"));
        }
        if (master.backgroundStyle.kind != FillKind::Plain) {
            xml.writeAttribute(QStringLiteral("qq:background-style"),
                               Fodp::backgroundJson(Background{true, master.background, master.backgroundStyle}));
        }
        if (!master.shapes.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:shapes"), Fodp::shapesJson(master.shapes));
        }
        // M75: the theme, text styles and placeholders the master view edits.
        if (!master.theme.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:theme"), Fodp::themeJson(master.theme));
        }
        if (!master.titleStyle.isEmpty() || !master.bodyStyle.isEmpty() || !master.otherStyle.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:text-styles"), Fodp::textStylesJson(master));
        }
        if (!master.placeholders.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:placeholders"), Fodp::placeholdersJson(master.placeholders));
        }
        if (!master.order.isEmpty()) {
            xml.writeAttribute(QStringLiteral("qq:order"), master.order.join(QLatin1Char(' ')));
        }
        writeMasterDecorations(xml, styles, master);
        xml.writeEndElement(); // style:master-page
    }
    xml.writeEndElement(); // master-styles
}

} // namespace

QByteArray writeFodp(const Presentation &presentation, QString *error)
{
    Q_UNUSED(error);
    // Pass 1 registers every style the body uses (output discarded), so
    // office:automatic-styles — which precedes the body — is complete.
    StyleRegistry styles;
    {
        QBuffer scratch;
        scratch.open(QIODevice::WriteOnly);
        QXmlStreamWriter collect(&scratch);
        writeBody(collect, styles, presentation);
        writeMasterStyles(collect, styles, presentation);
    }

    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("office:document"));
    xml.writeAttribute(QStringLiteral("xmlns:office"), QString::fromUtf16(Odf::kOffice));
    xml.writeAttribute(QStringLiteral("xmlns:style"), QString::fromUtf16(Odf::kStyle));
    xml.writeAttribute(QStringLiteral("xmlns:text"), QString::fromUtf16(Odf::kText));
    xml.writeAttribute(QStringLiteral("xmlns:draw"), QString::fromUtf16(Odf::kDraw));
    xml.writeAttribute(QStringLiteral("xmlns:svg"), QString::fromUtf16(Odf::kSvg));
    xml.writeAttribute(QStringLiteral("xmlns:fo"), QString::fromUtf16(Odf::kFo));
    xml.writeAttribute(QStringLiteral("xmlns:presentation"), QString::fromUtf16(Odf::kPresentation));
    xml.writeAttribute(QStringLiteral("xmlns:smil"), QString::fromUtf16(Odf::kSmil));
    xml.writeAttribute(QStringLiteral("xmlns:xlink"), QString::fromUtf16(Odf::kXlink));
    xml.writeAttribute(QStringLiteral("xmlns:dc"), QStringLiteral("http://purl.org/dc/elements/1.1/"));
    xml.writeAttribute(QStringLiteral("xmlns:qq"), QString::fromUtf16(Odf::kQq));
    xml.writeAttribute(QStringLiteral("office:version"), QStringLiteral("1.3"));
    xml.writeAttribute(QStringLiteral("office:mimetype"),
                       QStringLiteral("application/vnd.oasis.opendocument.presentation"));

    if (!presentation.title.isEmpty()) {
        xml.writeStartElement(QStringLiteral("office:meta"));
        xml.writeTextElement(QStringLiteral("dc:title"), presentation.title);
        xml.writeEndElement(); // office:meta
    }
    xml.writeStartElement(QStringLiteral("office:styles"));
    // The arrowhead the Arrow kind ends in (about as wide as it is long,
    // like the renderer's barbs).
    xml.writeEmptyElement(QStringLiteral("draw:marker"));
    xml.writeAttribute(QStringLiteral("draw:name"), QString::fromUtf16(StyleRegistry::kArrowMarker));
    xml.writeAttribute(QStringLiteral("svg:viewBox"), QStringLiteral("0 0 20 17"));
    xml.writeAttribute(QStringLiteral("svg:d"), QStringLiteral("M10 0l-10 17h20z"));
    xml.writeEndElement(); // office:styles

    xml.writeStartElement(QStringLiteral("office:automatic-styles"));
    styles.writeAutomaticStyles(xml, presentation.slideSize);
    xml.writeEndElement(); // office:automatic-styles
    writeMasterStyles(xml, styles, presentation);
    writeBody(xml, styles, presentation);
    xml.writeEndElement(); // office:document
    xml.writeEndDocument();
    return out;
}

} // namespace QindaOffice::Show
