// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Builds small .docx packages from WordprocessingML snippets for the
// office_docx tests: the snippets are what Word writes, typed by hand, so
// every test reads a real package through the real reader.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QString>

namespace QindaOffice::Test {

inline const char *kWordNamespaces =
    R"( xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main")"
    R"( xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")"
    R"( xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing")"
    R"( xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main")"
    R"( xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture")"
    R"( xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart")"
    R"( xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006")"
    R"( xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml")"
    R"( xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math")";

struct DocxFixture {
    QString body;      // children of w:body (a w:sectPr may be included)
    QString styles;    // children of w:styles; empty = no styles part
    QString numbering; // children of w:numbering; empty = no numbering part
    QString header;    // children of w:hdr; empty = none
    QString footer;    // children of w:ftr; empty = none
    QString footnotes; // children of w:footnotes; empty = none
    QString theme;     // a complete a:theme; empty = none
    QHash<QString, QByteArray> media; // rId → PNG bytes, stored as word/media/<rId>.png
    QHash<QString, QString> links;    // rId → external URL
    // Extra parts with a relationship from the document: rId → {part, type, content type, bytes}.
    struct Extra {
        QString part;
        QString relType;
        QString contentType;
        QByteArray bytes;
    };
    QHash<QString, Extra> extras;

    [[nodiscard]] QByteArray build() const
    {
        using namespace Ooxml;
        PackageWriter w;
        const QString main = QStringLiteral("word/document.xml");
        auto xml = [](const char *root, const QString &inner) {
            return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:%1%2>%3</w:%1>")
                .arg(QLatin1String(root), QLatin1String(kWordNamespaces), inner)
                .toUtf8();
        };
        const QString t = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");
        QString bodyXml = body;
        if (!bodyXml.contains(QStringLiteral("<w:sectPr"))) {
            bodyXml += QStringLiteral("<w:sectPr>%1%2<w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                                      "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\""
                                      " w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")
                           .arg(header.isEmpty() ? QString() : QStringLiteral("<w:headerReference w:type=\"default\" r:id=\"rIdHeader\"/>"),
                                footer.isEmpty() ? QString() : QStringLiteral("<w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/>"));
        }
        w.addPart(main, xml("document", QStringLiteral("<w:body>%1</w:body>").arg(bodyXml)), t + QStringLiteral("document.main+xml"));
        w.addRelationship(QString(), QString::fromUtf16(RelType::OfficeDocument), main);
        auto part = [&](const QString &name, const char *root, const QString &inner, const char16_t *rel,
                        const char *type, const QString &id) {
            if (inner.isEmpty()) {
                return;
            }
            w.addPart(name, xml(root, inner), t + QString::fromLatin1(type));
            w.addRelationshipWithId(main, id, QString::fromUtf16(rel), name);
        };
        part(QStringLiteral("word/styles.xml"), "styles", styles, RelType::Styles, "styles+xml", QStringLiteral("rIdStyles"));
        part(QStringLiteral("word/numbering.xml"), "numbering", numbering, RelType::Numbering, "numbering+xml", QStringLiteral("rIdNumbering"));
        part(QStringLiteral("word/header1.xml"), "hdr", header, RelType::Header, "header+xml", QStringLiteral("rIdHeader"));
        part(QStringLiteral("word/footer1.xml"), "ftr", footer, RelType::Footer, "footer+xml", QStringLiteral("rIdFooter"));
        part(QStringLiteral("word/footnotes.xml"), "footnotes", footnotes, RelType::Footnotes, "footnotes+xml", QStringLiteral("rIdNotes"));
        if (!theme.isEmpty()) {
            w.addPart(QStringLiteral("word/theme/theme1.xml"), theme.toUtf8(), QString::fromUtf16(ContentType::Theme));
            w.addRelationshipWithId(main, QStringLiteral("rIdTheme"), QString::fromUtf16(RelType::Theme),
                                    QStringLiteral("word/theme/theme1.xml"));
        }
        for (auto it = media.cbegin(); it != media.cend(); ++it) {
            const QString name = QStringLiteral("word/media/%1.png").arg(it.key());
            w.addPart(name, it.value());
            w.addRelationshipWithId(main, it.key(), QString::fromUtf16(RelType::Image), name);
            if (!header.isEmpty()) {
                w.addRelationshipWithId(QStringLiteral("word/header1.xml"), it.key(), QString::fromUtf16(RelType::Image), name);
            }
        }
        for (auto it = links.cbegin(); it != links.cend(); ++it) {
            w.addRelationshipWithId(main, it.key(), QString::fromUtf16(RelType::Hyperlink), it.value(), true);
        }
        for (auto it = extras.cbegin(); it != extras.cend(); ++it) {
            w.addPart(it->part, it->bytes, it->contentType);
            w.addRelationshipWithId(main, it.key(), it->relType, it->part);
        }
        return w.toZip();
    }
};

inline QByteArray pngBytes(int width, int height, QColor color = Qt::red)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A w:p with one run of text.
inline QString para(const QString &text, const QString &pPr = QString(), const QString &rPr = QString())
{
    return QStringLiteral("<w:p>%1<w:r>%2<w:t xml:space=\"preserve\">%3</w:t></w:r></w:p>")
        .arg(pPr.isEmpty() ? QString() : QStringLiteral("<w:pPr>%1</w:pPr>").arg(pPr),
             rPr.isEmpty() ? QString() : QStringLiteral("<w:rPr>%1</w:rPr>").arg(rPr), text);
}

} // namespace QindaOffice::Test
