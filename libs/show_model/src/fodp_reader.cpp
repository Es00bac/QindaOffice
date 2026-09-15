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

        if (xml.isStartElement() && ns == QLatin1String("draw") && name == QLatin1String("page")) {
            inSlide = true;
            currentSlide = Slide{};
            const auto attrs = xml.attributes();
            currentSlide.id = attrs.value(QStringLiteral("draw:name")).toString();
            currentSlide.title = attrs.value(QStringLiteral("qq:title")).toString();
            currentSlide.layout = attrs.value(QStringLiteral("qq:layout")).toString();
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
            const QString transform = attrs.value(QStringLiteral("draw:transform")).toString();
            if (transform.startsWith(QLatin1String("rotate ("))) {
                currentShape.rotation =
                    transform.mid(8, transform.indexOf(QLatin1Char(')')) - 8).toDouble();
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
                currentShape.kind = ShapeKind::TextBox;
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
                xml.readNext(); // text node
                currentParagraph.runs.append(readRun(xml.attributes(), xml.text().toString()));
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
