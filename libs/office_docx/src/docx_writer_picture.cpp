// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures of the .docx writer: a picture QindaWrite owns becomes a w:drawing
// with its original bytes; one read from Word keeps its own drawing (alt text,
// crop, effects, anchoring) with only its extents patched when resized, and
// its wp:anchor rewritten only when its wrap or position changed (M77,
// docx_float.h). Split from docx_writer_body.cpp.

#include "docx_float.h"
#include "docx_format.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QUrl>

#include <functional>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kWP = QString::fromUtf16(Ooxml::Ns::WP);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kPIC = QString::fromUtf16(Ooxml::Ns::PIC);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

// A preserved w:drawing at a new size: the anchor's (or inline's) extent
// and the picture shape's own a:ext.
void resizeDrawing(XmlElement &drawing, const QString &cx, const QString &cy)
{
    for (XmlChild &child : drawing.children) {
        if (!child.element || child.element->ns != kWP) {
            continue; // wp:inline or wp:anchor
        }
        for (XmlChild &part : child.element->children) {
            if (part.element && part.element->ns == kWP && part.element->name == u"extent") {
                part.element->setAttribute(QString(), QStringLiteral("cx"), cx);
                part.element->setAttribute(QString(), QStringLiteral("cy"), cy);
            }
        }
    }
    const QString kPicNs = QString::fromUtf16(Ooxml::Ns::PIC);
    if (auto *spPr = const_cast<XmlElement *>(drawing.descendant(kPicNs, u"spPr"))) {
        if (XmlElement *xfrm = spPr->child(QString::fromUtf16(Ooxml::Ns::A), u"xfrm")) {
            if (XmlElement *ext = xfrm->child(QString::fromUtf16(Ooxml::Ns::A), u"ext")) {
                ext->setAttribute(QString(), QStringLiteral("cx"), cx);
                ext->setAttribute(QString(), QStringLiteral("cy"), cy);
            }
        }
    }
}

QString imageExtension(const QByteArray &bytes)
{
    if (bytes.startsWith("\x89PNG")) {
        return QStringLiteral("png");
    }
    if (bytes.startsWith("\xFF\xD8")) {
        return QStringLiteral("jpeg");
    }
    if (bytes.startsWith("GIF8")) {
        return QStringLiteral("gif");
    }
    if (bytes.startsWith("BM")) {
        return QStringLiteral("bmp");
    }
    if (bytes.startsWith("II*") || bytes.startsWith("MM\0*")) {
        return QStringLiteral("tiff");
    }
    if (bytes.size() > 44 && bytes.mid(40, 4) == " EMF") {
        return QStringLiteral("emf");
    }
    if (bytes.startsWith("\xD7\xCD\xC6\x9A")) {
        return QStringLiteral("wmf");
    }
    if (bytes.contains("<svg")) {
        return QStringLiteral("svg");
    }
    return {};
}

} // namespace

XmlElementPtr BodyWriter::pictureRun(const QTextImageFormat &format)
{
    const QString name = format.name();
    QByteArray bytes = m_env.parts->media.value(name);
    QSizeF natural;
    if (bytes.isEmpty()) {
        const QVariant resource = m_document->resource(QTextDocument::ImageResource, QUrl(name));
        QImage image;
        if (resource.userType() == QMetaType::QImage) {
            image = resource.value<QImage>();
        } else if (resource.userType() == QMetaType::QPixmap) {
            image = resource.value<QPixmap>().toImage();
        } else if (resource.userType() == QMetaType::QByteArray) {
            bytes = resource.toByteArray();
        }
        if (bytes.isEmpty() && image.isNull()) {
            const QUrl url(name);
            QFile file(url.isLocalFile() ? url.toLocalFile() : name);
            if (file.open(QIODevice::ReadOnly)) {
                bytes = file.readAll();
            }
        }
        if (bytes.isEmpty() && !image.isNull()) {
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
        }
    }
    QString ext = imageExtension(bytes);
    if (bytes.isEmpty() || ext.isEmpty()) {
        return nullptr;
    }
    if (const QImage probe = QImage::fromData(bytes); !probe.isNull()) {
        natural = QSizeF(probe.width(), probe.height());
    }
    const QString media = m_env.mediaPart(bytes, ext);
    const QString rId = m_env.writer.addRelationship(m_part, QString::fromUtf16(Ooxml::RelType::Image), media);
    QSizeF size(format.width(), format.height());
    if (size.width() <= 0 && size.height() <= 0) {
        size = natural.isEmpty() ? QSizeF(96, 96) : natural;
    } else if (size.height() <= 0) {
        size.setHeight(natural.isEmpty() ? size.width() : size.width() * natural.height() / natural.width());
    } else if (size.width() <= 0) {
        size.setWidth(natural.isEmpty() ? size.height() : size.height() * natural.width() / natural.height());
    }
    const QString cx = QString::number(Units::roundToInt64(Units::pxToEmu(size.width())));
    const QString cy = QString::number(Units::roundToInt64(Units::pxToEmu(size.height())));

    auto run = std::make_shared<XmlElement>(kW, QStringLiteral("r"));
    if (XmlElementPtr rPr = runProperties(format)) {
        run->appendElement(rPr);
    }
    const QSizeF imported = format.property(kDrawingSize).toSizeF();
    XmlElementPtr drawing = Ooxml::parseFragment(format.stringProperty(kDrawingXml));
    const QString alt = format.stringProperty(kPictureAltText);
    if (drawing) {
        // The source drawing (alt text, crop, effects, anchoring) with the
        // picture re-pointed at the part written above; a resized picture
        // keeps all of it and only its extents change.
        if (qAbs(imported.width() - size.width()) >= 0.5 || qAbs(imported.height() - size.height()) >= 0.5) {
            resizeDrawing(*drawing, cx, cy);
        }
        // Wrap Text, Position (M77): the anchor rewritten only when changed.
        if (format.hasProperty(kPictureFloat)
            && format.property(kPictureFloat).toMap() != format.property(kPictureFloatSnapshot).toMap()) {
            writePictureFloat(*drawing, pictureFloat(format), cx, cy);
        }
        if (auto *docPr = const_cast<XmlElement *>(drawing->descendant(kWP, u"docPr"));
            docPr != nullptr && docPr->attr(u"descr") != alt) {
            if (alt.isEmpty()) {
                docPr->removeAttribute(QString(), u"descr");
            } else {
                docPr->setAttribute(QString(), QStringLiteral("descr"), alt);
            }
        }
        if (XmlElement *blip = const_cast<XmlElement *>(drawing->descendant(kA, u"blip"))) {
            blip->removeAttribute(kR, u"embed");
            if (m_env.copier) {
                m_env.copier->rewrite(*drawing, format.stringProperty(kInlineSourcePart), m_part);
            }
            blip->setAttribute(kR, QStringLiteral("embed"), rId);
        }
        std::function<void(XmlElement &)> renumber = [&](XmlElement &e) {
            if (e.ns == kWP && e.name == u"docPr") {
                e.setAttribute(QString(), QStringLiteral("id"), QString::number(m_env.nextDocPr++));
            }
            for (XmlChild &c : e.children) {
                if (c.element) {
                    renumber(*c.element);
                }
            }
        };
        renumber(*drawing);
        run->appendElement(drawing);
        return run;
    }
    const int id = m_env.nextDocPr++;
    XmlElement &d = run->appendElement(kW, QStringLiteral("drawing"));
    XmlElement &inl = d.appendElement(kWP, QStringLiteral("inline"));
    for (const char *attr : {"distT", "distB", "distL", "distR"}) {
        inl.setAttribute(QString(), QString::fromLatin1(attr), QStringLiteral("0"));
    }
    inl.appendElement(kWP, QStringLiteral("extent"))
        .setAttribute(QString(), QStringLiteral("cx"), cx)
        .setAttribute(QString(), QStringLiteral("cy"), cy);
    XmlElement &effect = inl.appendElement(kWP, QStringLiteral("effectExtent"));
    for (const char *attr : {"l", "t", "r", "b"}) {
        effect.setAttribute(QString(), QString::fromLatin1(attr), QStringLiteral("0"));
    }
    XmlElement &docPr = inl.appendElement(kWP, QStringLiteral("docPr"))
                            .setAttribute(QString(), QStringLiteral("id"), QString::number(id))
                            .setAttribute(QString(), QStringLiteral("name"), QStringLiteral("Picture %1").arg(id));
    if (!alt.isEmpty()) {
        docPr.setAttribute(QString(), QStringLiteral("descr"), alt);
    }
    inl.appendElement(kWP, QStringLiteral("cNvGraphicFramePr"))
        .appendElement(kA, QStringLiteral("graphicFrameLocks"))
        .setAttribute(QString(), QStringLiteral("noChangeAspect"), QStringLiteral("1"));
    XmlElement &data = inl.appendElement(kA, QStringLiteral("graphic")).appendElement(kA, QStringLiteral("graphicData"));
    data.setAttribute(QString(), QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/picture"));
    XmlElement &pic = data.appendElement(kPIC, QStringLiteral("pic"));
    XmlElement &nv = pic.appendElement(kPIC, QStringLiteral("nvPicPr"));
    nv.appendElement(kPIC, QStringLiteral("cNvPr"))
        .setAttribute(QString(), QStringLiteral("id"), QStringLiteral("0"))
        .setAttribute(QString(), QStringLiteral("name"), media.section(QLatin1Char('/'), -1));
    nv.appendElement(kPIC, QStringLiteral("cNvPicPr"));
    XmlElement &fill = pic.appendElement(kPIC, QStringLiteral("blipFill"));
    fill.appendElement(kA, QStringLiteral("blip")).setAttribute(kR, QStringLiteral("embed"), rId);
    fill.appendElement(kA, QStringLiteral("stretch")).appendElement(kA, QStringLiteral("fillRect"));
    XmlElement &sp = pic.appendElement(kPIC, QStringLiteral("spPr"));
    XmlElement &xfrm = sp.appendElement(kA, QStringLiteral("xfrm"));
    xfrm.appendElement(kA, QStringLiteral("off"))
        .setAttribute(QString(), QStringLiteral("x"), QStringLiteral("0"))
        .setAttribute(QString(), QStringLiteral("y"), QStringLiteral("0"));
    xfrm.appendElement(kA, QStringLiteral("ext"))
        .setAttribute(QString(), QStringLiteral("cx"), cx)
        .setAttribute(QString(), QStringLiteral("cy"), cy);
    XmlElement &geom = sp.appendElement(kA, QStringLiteral("prstGeom"));
    geom.setAttribute(QString(), QStringLiteral("prst"), QStringLiteral("rect"));
    geom.appendElement(kA, QStringLiteral("avLst"));
    if (const PictureFloat layout = pictureFloat(format); layout.isFloating()) {
        writePictureFloat(d, layout, cx, cy); // wp:inline → Word's wp:anchor
    }
    return run;
}

} // namespace QindaOffice::Docx::detail
