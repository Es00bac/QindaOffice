// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_namespaces.h"

#include <array>
#include <utility>

namespace QindaOffice::Ooxml::Ns {

namespace {

struct PrefixRow {
    const char16_t *uri;
    const char *prefix;
};

// Word's own prefixes for everything it writes by default (see the
// AGENT-NOTE in the header).
constexpr std::array kPrefixes{
    PrefixRow{W, "w"},
    PrefixRow{R, "r"},
    PrefixRow{WP, "wp"},
    PrefixRow{A, "a"},
    PrefixRow{PIC, "pic"},
    PrefixRow{C, "c"},
    PrefixRow{DGM, "dgm"},
    PrefixRow{P, "p"},
    PrefixRow{S, "x"},
    PrefixRow{M, "m"},
    PrefixRow{MC, "mc"},
    PrefixRow{V, "v"},
    PrefixRow{O, "o"},
    PrefixRow{W10, "w10"},
    PrefixRow{XML, "xml"},
    PrefixRow{WPS, "wps"},
    PrefixRow{WPG, "wpg"},
    PrefixRow{WPC, "wpc"},
    PrefixRow{WPI, "wpi"},
    PrefixRow{WP14, "wp14"},
    PrefixRow{W14, "w14"},
    PrefixRow{W15, "w15"},
    PrefixRow{A14, "a14"},
    PrefixRow{P14, "p14"},
    PrefixRow{CP, "cp"},
    PrefixRow{DC, "dc"},
    PrefixRow{DCTERMS, "dcterms"},
    PrefixRow{DCMITYPE, "dcmitype"},
    PrefixRow{XSI, "xsi"},
    PrefixRow{EP, "ep"},
    PrefixRow{VT, "vt"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2016/wordml/cid", "w16cid"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2018/wordml", "w16"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2018/wordml/cex", "w16cex"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2015/wordml/symex", "w16se"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash", "w16sdtdh"},
    PrefixRow{u"http://schemas.microsoft.com/office/word/2006/wordml", "wne"},
    PrefixRow{u"http://schemas.microsoft.com/office/drawing/2014/main", "a16"},
    PrefixRow{u"http://schemas.microsoft.com/office/drawing/2014/chartex", "cx"},
    PrefixRow{u"http://schemas.openxmlformats.org/drawingml/2006/chartDrawing", "cdr"},
    PrefixRow{u"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing", "xdr"},
    PrefixRow{u"http://schemas.openxmlformats.org/officeDocument/2006/customXml", "ds"},
    PrefixRow{u"http://schemas.openxmlformats.org/officeDocument/2006/bibliography", "b"},
    PrefixRow{u"http://schemas.microsoft.com/office/powerpoint/2012/main", "p15"},
    PrefixRow{u"http://schemas.microsoft.com/office/powerpoint/2010/main", "p14"},
    PrefixRow{u"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main", "x14"},
    PrefixRow{u"http://schemas.microsoft.com/office/spreadsheetml/2009/9/ac", "x14ac"},
    PrefixRow{u"urn:schemas-microsoft-com:office:excel", "x"},
    PrefixRow{u"urn:schemas-microsoft-com:office:powerpoint", "pvml"},
};

// Strict → Transitional (ISO/IEC 29500-1 Annex; ECMA-376 Part 1 §A).
constexpr std::array<std::pair<const char16_t *, const char16_t *>, 12> kStrict{{
    {u"http://purl.oclc.org/ooxml/wordprocessingml/main", W},
    {u"http://purl.oclc.org/ooxml/officeDocument/relationships", R},
    {u"http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing", WP},
    {u"http://purl.oclc.org/ooxml/drawingml/main", A},
    {u"http://purl.oclc.org/ooxml/drawingml/picture", PIC},
    {u"http://purl.oclc.org/ooxml/drawingml/chart", C},
    {u"http://purl.oclc.org/ooxml/drawingml/diagram", DGM},
    {u"http://purl.oclc.org/ooxml/presentationml/main", P},
    {u"http://purl.oclc.org/ooxml/spreadsheetml/main", S},
    {u"http://purl.oclc.org/ooxml/officeDocument/math", M},
    {u"http://purl.oclc.org/ooxml/officeDocument/extendedProperties", EP},
    {u"http://purl.oclc.org/ooxml/officeDocument/docPropsVTypes", VT},
}};

} // namespace

QString conventionalPrefix(QStringView uri)
{
    for (const PrefixRow &row : kPrefixes) {
        if (uri == QStringView(row.uri)) {
            return QString::fromLatin1(row.prefix);
        }
    }
    return {};
}

QString normalizeNamespace(QStringView uri)
{
    if (!uri.startsWith(u"http://purl.oclc.org/ooxml/")) {
        return uri.toString();
    }
    for (const auto &[strict, transitional] : kStrict) {
        if (uri == QStringView(strict)) {
            return QString::fromUtf16(transitional);
        }
    }
    return uri.toString();
}

} // namespace QindaOffice::Ooxml::Ns

namespace QindaOffice::Ooxml::RelType {

QString normalizeRelationshipType(QStringView type)
{
    // Strict relationship types share the Transitional local names under a
    // different stem, e.g. …/ooxml/officeDocument/relationships/styles.
    static constexpr QStringView kStrictStem = u"http://purl.oclc.org/ooxml/officeDocument/relationships/";
    static constexpr QStringView kTransitionalStem =
        u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
    if (type.startsWith(kStrictStem)) {
        return kTransitionalStem.toString() + type.mid(kStrictStem.size()).toString();
    }
    return type.toString();
}

} // namespace QindaOffice::Ooxml::RelType

namespace QindaOffice::Ooxml::ContentType {

QString forImageExtension(QStringView extension)
{
    const QString ext = extension.toString().toLower();
    if (ext == u"png") {
        return QStringLiteral("image/png");
    }
    if (ext == u"jpg" || ext == u"jpeg" || ext == u"jpe" || ext == u"jfif") {
        return QStringLiteral("image/jpeg");
    }
    if (ext == u"gif") {
        return QStringLiteral("image/gif");
    }
    if (ext == u"bmp") {
        return QStringLiteral("image/bmp");
    }
    if (ext == u"tif" || ext == u"tiff") {
        return QStringLiteral("image/tiff");
    }
    if (ext == u"emf") {
        return QStringLiteral("image/x-emf");
    }
    if (ext == u"wmf") {
        return QStringLiteral("image/x-wmf");
    }
    if (ext == u"svg") {
        return QStringLiteral("image/svg+xml");
    }
    if (ext == u"webp") {
        return QStringLiteral("image/webp");
    }
    if (ext == u"ico") {
        return QStringLiteral("image/x-icon");
    }
    return {};
}

} // namespace QindaOffice::Ooxml::ContentType
