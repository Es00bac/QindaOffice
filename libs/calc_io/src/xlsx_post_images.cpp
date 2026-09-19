// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_post_images.h"

#include "xlsx_xml.h"

#include <QDir>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

constexpr int kEmuPerPx = 9525;

QString relsPathFor(const QString &part)
{
    const int slash = part.lastIndexOf(QLatin1Char('/'));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash)
        + QStringLiteral(".rels");
}

bool insertBeforeFirst(QByteArray *xml, const QStringList &candidates, const QByteArray &fragment)
{
    int earliest = -1;
    for (const QString &candidate : candidates) {
        const int found = xml->indexOf(candidate.toUtf8());
        if (found >= 0 && (earliest < 0 || found < earliest)) {
            earliest = found;
        }
    }
    if (earliest < 0) {
        return false;
    }
    xml->insert(earliest, fragment);
    return true;
}

int nextShapeId(const QByteArray &drawingXml)
{
    int maxId = 0;
    static const QRegularExpression idAttr(QStringLiteral("id=\"(\\d+)\""));
    auto it = idAttr.globalMatch(QString::fromUtf8(drawingXml));
    while (it.hasNext()) {
        maxId = qMax(maxId, it.next().captured(1).toInt());
    }
    return maxId + 1;
}

QString drawingPartFor(const QByteArray &sheetRelsXml)
{
    QXmlStreamReader reader(sheetRelsXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto attributes = reader.attributes();
        if (attributes.value(QLatin1String("Type")).toString()
            .endsWith(QLatin1String("/drawing"))) {
            return QDir::cleanPath(QStringLiteral("xl/worksheets/")
                                   + attributes.value(QLatin1String("Target")).toString());
        }
    }
    return {};
}

// Adds a <Default Extension ContentType> for a media extension. Packages
// carry defaults for png/jpeg already; anything else has to be declared or
// the consumer refuses the part.
void appendContentTypeDefault(QByteArray *contentTypesXml, const QString &extension,
                              const QString &contentType)
{
    const QByteArray probe = QByteArrayLiteral("Extension=\"") + extension.toUtf8()
        + QByteArrayLiteral("\"");
    if (contentTypesXml->contains(probe)) {
        return;
    }
    const QByteArray entry = QByteArrayLiteral("<Default Extension=\"") + extension.toUtf8()
        + QByteArrayLiteral("\" ContentType=\"") + contentType.toUtf8()
        + QByteArrayLiteral("\"/>");
    const int types = contentTypesXml->indexOf("<Types");
    const int close = types >= 0 ? contentTypesXml->indexOf('>', types) : -1;
    if (close >= 0) {
        contentTypesXml->insert(close + 1, entry);
    }
}

// A oneCellAnchor holding a picture: position from the anchor cell, size in
// EMU from the spec (the author's extent, not the image's own DPI).
QByteArray pictureAnchorXml(const QString &rid, int shapeId, const ImageSpec &spec)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("xdr:oneCellAnchor"));
    writer.writeStartElement(QStringLiteral("xdr:from"));
    writer.writeTextElement(QStringLiteral("xdr:col"), QString::number(spec.anchorCol));
    writer.writeTextElement(QStringLiteral("xdr:colOff"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("xdr:row"), QString::number(spec.anchorRow));
    writer.writeTextElement(QStringLiteral("xdr:rowOff"), QStringLiteral("0"));
    writer.writeEndElement(); // from
    writer.writeEmptyElement(QStringLiteral("xdr:ext"));
    writer.writeAttribute(QStringLiteral("cx"),
                          QString::number(qint64(qMax(spec.widthPx, 1)) * kEmuPerPx));
    writer.writeAttribute(QStringLiteral("cy"),
                          QString::number(qint64(qMax(spec.heightPx, 1)) * kEmuPerPx));

    writer.writeStartElement(QStringLiteral("xdr:pic"));
    writer.writeStartElement(QStringLiteral("xdr:nvPicPr"));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), QString::number(shapeId));
    writer.writeAttribute(QStringLiteral("name"), QStringLiteral("Picture %1").arg(shapeId));
    writer.writeStartElement(QStringLiteral("xdr:cNvPicPr"));
    writer.writeEmptyElement(QStringLiteral("a:picLocks"));
    writer.writeAttribute(QStringLiteral("noChangeAspect"), QStringLiteral("1"));
    writer.writeEndElement(); // cNvPicPr
    writer.writeEndElement(); // nvPicPr

    writer.writeStartElement(QStringLiteral("xdr:blipFill"));
    writer.writeEmptyElement(QStringLiteral("a:blip"));
    writer.writeAttribute(QStringLiteral("r:embed"), rid);
    writer.writeStartElement(QStringLiteral("a:stretch"));
    writer.writeEmptyElement(QStringLiteral("a:fillRect"));
    writer.writeEndElement(); // stretch
    writer.writeEndElement(); // blipFill

    writer.writeStartElement(QStringLiteral("xdr:spPr"));
    writer.writeStartElement(QStringLiteral("a:xfrm"));
    writer.writeEmptyElement(QStringLiteral("a:off"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
    writer.writeEmptyElement(QStringLiteral("a:ext"));
    writer.writeAttribute(QStringLiteral("cx"),
                          QString::number(qint64(qMax(spec.widthPx, 1)) * kEmuPerPx));
    writer.writeAttribute(QStringLiteral("cy"),
                          QString::number(qint64(qMax(spec.heightPx, 1)) * kEmuPerPx));
    writer.writeEndElement(); // xfrm
    writer.writeStartElement(QStringLiteral("a:prstGeom"));
    writer.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    writer.writeEmptyElement(QStringLiteral("a:avLst"));
    writer.writeEndElement(); // prstGeom
    writer.writeEndElement(); // spPr
    writer.writeEndElement(); // pic

    writer.writeEmptyElement(QStringLiteral("xdr:clientData"));
    writer.writeEndElement(); // oneCellAnchor
    return xml;
}

} // namespace

bool ensureDrawingPart(QHash<QString, QByteArray> *parts, const QString &sheetPart,
                       int *drawingPartCount, QString *drawingPart, QString *error)
{
    const QString sheetRelsPath = relsPathFor(sheetPart);
    QByteArray sheetRelsXml = parts->value(sheetRelsPath);
    *drawingPart = drawingPartFor(sheetRelsXml);
    if (!drawingPart->isEmpty()) {
        return true;
    }
    *drawingPart = QStringLiteral("xl/drawings/drawing%1.xml").arg(++(*drawingPartCount));
    // Written open/close, never self-closed: the anchors are inserted before
    // the end tag, and a <xdr:wsDr/> skeleton has none to find.
    parts->insert(*drawingPart,
                  QByteArrayLiteral(
                      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/"
                      "spreadsheetDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/"
                      "drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/"
                      "officeDocument/2006/relationships\">"
                      "</xdr:wsDr>"));
    appendContentTypeOverride(
        &(*parts)[QStringLiteral("[Content_Types].xml")], QStringLiteral("/") + *drawingPart,
        QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"));
    const QString drawingRid = appendRelationship(
        &sheetRelsXml,
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing"),
        QStringLiteral("../drawings/")
            + drawingPart->mid(drawingPart->lastIndexOf(QLatin1Char('/')) + 1));
    if (drawingRid.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass: broken rels part %1").arg(sheetRelsPath);
        }
        return false;
    }
    // <drawing> precedes legacyDrawing/picture/tableParts/extLst in the
    // worksheet schema; the close tag is the fallback anchor.
    if (!insertBeforeFirst(&(*parts)[sheetPart],
                           {QStringLiteral("<legacyDrawing"), QStringLiteral("<picture"),
                            QStringLiteral("<tableParts"), QStringLiteral("<extLst"),
                            QStringLiteral("</worksheet>")},
                           QByteArrayLiteral("<drawing r:id=\"") + drawingRid.toUtf8()
                               + QByteArrayLiteral("\"/>"))) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass: no drawing anchor in %1").arg(sheetPart);
        }
        return false;
    }
    parts->insert(sheetRelsPath, sheetRelsXml);
    return true;
}

bool applyImagePatches(QHash<QString, QByteArray> *parts,
                       const QList<PatchManifest::ImagePatch> &images, int *drawingPartCount,
                       int *mediaCount, QString *error)
{
    for (const PatchManifest::ImagePatch &patch : images) {
        if (!patch.spec.isValid()) {
            continue;
        }
        if (!parts->contains(patch.sheetPart)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: missing sheet part %1").arg(patch.sheetPart);
            }
            return false;
        }
        const QString format = patch.spec.format.isEmpty() ? QStringLiteral("png")
                                                           : patch.spec.format;
        const QString mediaPart =
            QStringLiteral("xl/media/image%1.%2").arg(++(*mediaCount)).arg(format);
        parts->insert(mediaPart, *patch.spec.bytes);
        appendContentTypeDefault(&(*parts)[QStringLiteral("[Content_Types].xml")], format,
                                 imageContentType(format));

        QString drawingPart;
        if (!ensureDrawingPart(parts, patch.sheetPart, drawingPartCount, &drawingPart, error)) {
            return false;
        }
        const QString drawingRelsPath = relsPathFor(drawingPart);
        QByteArray drawingRelsXml = parts->value(drawingRelsPath);
        const QString rid = appendRelationship(
            &drawingRelsXml,
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"),
            // Relative to xl/drawings, media lives one level up.
            QStringLiteral("../media/") + mediaPart.mid(mediaPart.lastIndexOf(QLatin1Char('/')) + 1));
        if (rid.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: broken rels part %1").arg(drawingRelsPath);
            }
            return false;
        }
        parts->insert(drawingRelsPath, drawingRelsXml);

        QByteArray &drawingXml = (*parts)[drawingPart];
        const QByteArray anchor =
            pictureAnchorXml(rid, nextShapeId(drawingXml), patch.spec);
        if (!insertBeforeFirst(&drawingXml, {QStringLiteral("</xdr:wsDr>")}, anchor)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: malformed drawing %1").arg(drawingPart);
            }
            return false;
        }
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
