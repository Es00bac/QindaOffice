// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures and everything else that lives inside a run: DrawingML
// pictures (inline, and floating with their wrap and position — M77,
// docx_float.h), VML pictures and text boxes, charts,
// SmartArt, shapes, OLE objects and equations. Pictures keep their bytes;
// M93 graphics carry native editable content and their original markup.
// Unsupported objects remain preserved verbatim with a preview (D-068).

#include "docx_float.h"
#include "docx_graphics.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kWP = QString::fromUtf16(Ooxml::Ns::WP);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kV = QString::fromUtf16(Ooxml::Ns::V);
const QString kO = QString::fromUtf16(Ooxml::Ns::O);
const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);

// Namespaces whose mc:Choice the reader can show (as a picture or box).
bool understood(const QString &requirement)
{
    static const QStringList kKnown{QString::fromUtf16(Ooxml::Ns::WPS), QString::fromUtf16(Ooxml::Ns::WPG),
                                    QString::fromUtf16(Ooxml::Ns::WPC), QString::fromUtf16(Ooxml::Ns::WPI),
                                    QString::fromUtf16(Ooxml::Ns::WP14), QString::fromUtf16(Ooxml::Ns::W14),
                                    QString::fromUtf16(Ooxml::Ns::A14)};
    for (const QString &token : requirement.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!kKnown.contains(token.mid(1, token.size() - 2))) {
            return false;
        }
    }
    return true;
}

// The text of every w:t below `e`, paragraphs separated by newlines.
QString paragraphText(const XmlElement &e)
{
    QStringList lines;
    std::function<void(const XmlElement &)> walk = [&](const XmlElement &node) {
        for (const XmlElement *c : node.elements()) {
            if (c->ns == kW && c->name == u"p") {
                QString line;
                std::function<void(const XmlElement &)> text = [&](const XmlElement &n) {
                    for (const XmlElement *t : n.elements()) {
                        if (t->ns == kW && t->name == u"t") {
                            line += t->text();
                        } else if (t->ns == kW && t->name == u"tab") {
                            line += QLatin1Char('\t');
                        } else {
                            text(*t);
                        }
                    }
                };
                text(*c);
                lines.append(line);
            } else {
                walk(*c);
            }
        }
    };
    walk(e);
    return lines.join(QLatin1Char('\n')).trimmed();
}

// VML sizes come from the CSS-like style attribute ("width:120pt;height:3in").
QSizeF vmlSize(const XmlElement &shape)
{
    QSizeF size(96, 96);
    const QString style = shape.attr(u"style");
    for (const QString &decl : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString key = decl.section(QLatin1Char(':'), 0, 0).trimmed();
        const QString value = decl.section(QLatin1Char(':'), 1).trimmed();
        const auto inches = Units::parseUniversalMeasureInches(value);
        const qreal px = inches ? *inches * 96.0 : value.toDouble() * 96.0 / 72.0;
        if (key == u"width" && px > 0) {
            size.setWidth(px);
        } else if (key == u"height" && px > 0) {
            size.setHeight(px);
        }
    }
    return size;
}

} // namespace

QImage placeholderImage(const QSizeF &size, const QString &label, const QString &body)
{
    const QSize px(qBound(16, qRound(size.width()), 4000), qBound(16, qRound(size.height()), 4000));
    QImage image(px, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0xF3, 0xF4, 0xF6));
    QPainter painter(&image);
    painter.setPen(QPen(QColor(0x9C, 0xA3, 0xAF), 1, Qt::DashLine));
    painter.drawRect(QRectF(0.5, 0.5, px.width() - 1, px.height() - 1));
    QFont font = painter.font();
    font.setPixelSize(qBound(8, px.height() / 6, 13));
    painter.setFont(font);
    painter.setPen(QColor(0x37, 0x41, 0x51));
    const QRectF inner(6, 4, px.width() - 12, px.height() - 8);
    if (body.isEmpty()) {
        painter.drawText(inner, Qt::AlignCenter | Qt::TextWordWrap, label);
    } else {
        painter.drawText(inner, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, body);
    }
    return image;
}

bool BodyReader::readObject(const XmlElement &run, const XmlElement &object, QTextCursor &cursor,
                            const QTextCharFormat &format, InlineState &inl, ContainerState &state)
{
    // What to show, and how big.
    QString label;
    QString body;
    QSizeF size(96, 48);
    QByteArray pictureBytes;
    QString pictureName;
    bool nativeGraphic = false;
    QTextImageFormat graphicFormat;
    QImage graphicImage;
    bool isPlainPicture = false; // a DrawingML picture we can own outright
    const XmlElement *drawing = nullptr;
    PictureFloat layout;         // a wp:anchor's wrap and position

    const XmlElement *shown = &object;
    if (object.ns == kMC && object.name == u"AlternateContent") {
        shown = object.child(kMC, u"Fallback");
        for (const XmlElement *choice : object.elements(kMC, u"Choice")) {
            if (understood(choice->attr(u"Requires"))) {
                shown = choice;
                break;
            }
        }
        if (shown == nullptr || shown->elements().isEmpty()) {
            return false;
        }
        shown = shown->elements().first();
    }

    auto pictureFromRel = [&](const QString &id) {
        if (id.isEmpty()) {
            return;
        }
        const auto rel = m_env.package->relationship(m_part, id);
        if (!rel || rel->external) {
            return;
        }
        const QString target = m_env.package->targetPart(m_part, *rel);
        pictureBytes = m_env.package->data(target);
        pictureName = QFileInfo(target).fileName();
    };

    if (shown->ns == kW && shown->name == u"drawing") {
        drawing = shown;
        const XmlElement *frame = shown->child(kWP, u"inline");
        const bool anchored = frame == nullptr;
        if (anchored) {
            frame = shown->child(kWP, u"anchor");
        }
        if (frame == nullptr) {
            return false;
        }
        if (const XmlElement *extent = frame->child(kWP, u"extent")) {
            size = QSizeF(Units::emuToPx(Units::parseEmu(extent->attr(u"cx"))),
                          Units::emuToPx(Units::parseEmu(extent->attr(u"cy"))));
        }
        graphicFormat.setWidth(size.width());
        graphicFormat.setHeight(size.height());
        nativeGraphic = readGraphic(*frame, m_env.package, m_part, m_env.theme, graphicFormat, graphicImage);
        if (nativeGraphic && anchored) layout = readPictureFloat(*frame);
        if (nativeGraphic && graphicFormat.stringProperty(kGraphicKind) == u"smartArt"
            && graphicFormat.property(kGraphicData).toMap().value(QStringLiteral("drawingXml")).toString().isEmpty())
            m_env.approximations[QStringLiteral("SmartArt layout")] += 1;
        const XmlElement *data = frame->descendant(kA, u"graphicData");
        const QString uri = data != nullptr ? data->attr(u"uri") : QString();
        if (uri.endsWith(u"/picture")) {
            if (const XmlElement *blip = data->descendant(kA, u"blip")) {
                pictureFromRel(blip->attribute(kR, u"embed"));
            }
            isPlainPicture = !pictureBytes.isEmpty() && &object == shown;
            label = QStringLiteral("Picture");
            if (anchored) {
                layout = readPictureFloat(*frame);
            }
        } else if (uri.endsWith(u"/chart") || uri.contains(u"chartex")) {
            label = QStringLiteral("Chart");
            if (!nativeGraphic) m_env.approximations[QStringLiteral("chart")] += 1;
        } else if (uri.endsWith(u"/diagram")) {
            label = QStringLiteral("SmartArt graphic");
            if (!nativeGraphic) m_env.approximations[QStringLiteral("SmartArt graphic")] += 1;
        } else {
            body = paragraphText(*frame);
            label = body.isEmpty() ? QStringLiteral("Shape") : QStringLiteral("Text box");
            if (!nativeGraphic) m_env.approximations[body.isEmpty() ? QStringLiteral("shape") : QStringLiteral("text box")] += 1;
        }
    } else if (shown->ns == kW && (shown->name == u"pict" || shown->name == u"object")) {
        const XmlElement *shape = shown->descendant(kV, u"shape");
        if (shape == nullptr) {
            shape = shown->descendant(kV, u"rect");
        }
        if (shape != nullptr) {
            size = vmlSize(*shape);
        }
        if (const XmlElement *imageData = shown->descendant(kV, u"imagedata")) {
            QString id = imageData->attribute(kR, u"id");
            if (id.isEmpty()) {
                id = imageData->attribute(kO, u"relid");
            }
            pictureFromRel(id);
        }
        if (shown->name == u"object") {
            label = QStringLiteral("Embedded object");
            m_env.approximations[QStringLiteral("embedded object")] += 1;
        } else if (const XmlElement *textbox = shown->descendant(kW, u"txbxContent")) {
            body = paragraphText(*textbox);
            label = QStringLiteral("Text box");
            m_env.approximations[QStringLiteral("text box")] += 1;
        } else {
            label = pictureBytes.isEmpty() ? QStringLiteral("Shape") : QStringLiteral("Picture");
            if (pictureBytes.isEmpty()) {
                m_env.approximations[QStringLiteral("shape")] += 1;
            }
        }
    } else if (shown->ns == kW && shown->name == u"contentPart") {
        label = QStringLiteral("Ink");
        m_env.approximations[QStringLiteral("ink drawing")] += 1;
    } else {
        return false;
    }

    QTextImageFormat image;
    image.merge(format);
    QImage decoded = graphicImage;
    if (nativeGraphic) image.merge(graphicFormat);
    if (!pictureBytes.isEmpty()) {
        decoded = QImage::fromData(pictureBytes);
    }
    QString resource;
    if (isPlainPicture) {
        // A picture QindaWrite owns: original bytes in WordParts::media, the
        // decoded image as the document resource, the w:drawing kept for its
        // alt text, effects, cropping and anchoring.
        const QString ext = QFileInfo(pictureName).suffix().toLower();
        resource = QStringLiteral("%1:image%2.%3")
                       .arg(QString::fromUtf16(kMediaScheme))
                       .arg(m_env.nextMedia++)
                       .arg(ext.isEmpty() ? QStringLiteral("png") : ext);
        m_env.parts->media.insert(resource, pictureBytes);
        if (decoded.isNull()) {
            decoded = placeholderImage(size, QStringLiteral("Picture (%1)").arg(ext.toUpper()));
        }
        image.setProperty(kDrawingXml, Ooxml::serializeFragment(*drawing));
        image.setProperty(kDrawingSize, size);
        if (const XmlElement *docPr = drawing->descendant(QString::fromUtf16(Ooxml::Ns::WP), u"docPr")) {
            if (const QString alt = docPr->attr(u"descr"); !alt.isEmpty()) {
                image.setProperty(kPictureAltText, alt);
            }
        }
        image.setProperty(kInlineSourcePart, m_part);
        if (layout.isFloating()) {
            image.setProperty(kPictureFloat, layout.toMap());
            image.setProperty(kPictureFloatSnapshot, layout.toMap());
        }
    } else {
        // Preserved verbatim: the whole run goes back on save.
        resource = QStringLiteral("%1:object%2").arg(QString::fromUtf16(kMediaScheme)).arg(m_env.nextIsland);
        if (decoded.isNull()) {
            decoded = placeholderImage(size, label, body);
        }
        XmlElement preserved(kW, QStringLiteral("r"));
        if (const XmlElement *rPr = run.child(kW, u"rPr")) {
            preserved.appendElement(rPr->clone());
        }
        preserved.appendElement(object.clone());
        image.setProperty(kInlineXml, Ooxml::serializeFragment(preserved));
        image.setProperty(kInlineSourcePart, m_part);
        image.setProperty(kInlineSerial, m_env.nextIsland++);
        image.setProperty(kInlineLabel, label);
    }
    m_document->addResource(QTextDocument::ImageResource, QUrl(resource), decoded);
    image.setName(resource);
    image.setWidth(size.width());
    image.setHeight(size.height());
    if ((isPlainPicture || nativeGraphic) && layout.isFloating()) {
        image.setProperty(kPictureFloat, layout.toMap());
        image.setProperty(kPictureFloatSnapshot, layout.toMap());
        insertPicture(cursor, image, inl, state);
    } else {
        insertObject(cursor, image, inl, state);
    }
    return true;
}

void BodyReader::insertPicture(QTextCursor &cursor, const QTextImageFormat &image, InlineState &inl,
                               ContainerState &state)
{
    installPictureHandler(m_document);
    const QTextFrameFormat::Position where = floatPosition(pictureFloat(image));
    if (where == QTextFrameFormat::InFlow) {
        insertObject(cursor, image, inl, state);
        return;
    }
    cursor.insertImage(prepareFormat(image, inl, state).toImageFormat(), where);
    inl.blockHasContent = true;
}

void BodyReader::readMath(const XmlElement &math, QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    // Equations are kept verbatim and shown as their linear text in italics.
    QString text;
    std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
        for (const XmlElement *c : e.elements()) {
            if (c->ns == kM && c->name == u"t") {
                text += c->text();
            } else {
                walk(*c);
            }
        }
    };
    walk(math);
    QTextCharFormat format;
    format.setFontItalic(true);
    format.setFontFamilies({QStringLiteral("Cambria Math")});
    format.setProperty(kInlineXml, Ooxml::serializeFragment(math));
    format.setProperty(kInlineSourcePart, m_part);
    format.setProperty(kInlineSerial, m_env.nextIsland++);
    format.setProperty(kInlineLabel, QStringLiteral("Equation"));
    m_env.approximations[QStringLiteral("equation")] += 1;
    insertText(cursor, text.isEmpty() ? QStringLiteral("[equation]") : text, format, inl, state);
}

} // namespace QindaOffice::Docx::detail
