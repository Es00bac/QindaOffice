// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Namespace URIs, relationship types and content types of Office Open XML
// (ECMA-376, Transitional), plus the conventional prefix each one is written
// with. Every reader in the suite compares against the TRANSITIONAL URIs:
// the package and XML layers rewrite Strict URIs (purl.oclc.org) to their
// Transitional twins on load (normalizeNamespace / normalizeRelationshipType),
// so no format mapper has to know Strict exists.
//
// AGENT-NOTE: the prefixes are not cosmetic. mc:Ignorable and mc:Choice
// Requires name namespaces BY PREFIX, so a package written with Word's own
// prefixes (w14, wp14, wps, …) is the one every consumer has been tested
// against. XmlWriter uses this table for every element it emits, including
// XML preserved verbatim from a source document.

#include <QString>
#include <QStringView>

namespace QindaOffice::Ooxml::Ns {

// Core OOXML vocabularies.
inline constexpr char16_t W[] = u"http://schemas.openxmlformats.org/wordprocessingml/2006/main";
inline constexpr char16_t R[] = u"http://schemas.openxmlformats.org/officeDocument/2006/relationships";
inline constexpr char16_t WP[] = u"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
inline constexpr char16_t A[] = u"http://schemas.openxmlformats.org/drawingml/2006/main";
inline constexpr char16_t PIC[] = u"http://schemas.openxmlformats.org/drawingml/2006/picture";
inline constexpr char16_t C[] = u"http://schemas.openxmlformats.org/drawingml/2006/chart";
inline constexpr char16_t DGM[] = u"http://schemas.openxmlformats.org/drawingml/2006/diagram";
inline constexpr char16_t P[] = u"http://schemas.openxmlformats.org/presentationml/2006/main";
inline constexpr char16_t S[] = u"http://schemas.openxmlformats.org/spreadsheetml/2006/main";
inline constexpr char16_t M[] = u"http://schemas.openxmlformats.org/officeDocument/2006/math";
inline constexpr char16_t MC[] = u"http://schemas.openxmlformats.org/markup-compatibility/2006";
inline constexpr char16_t V[] = u"urn:schemas-microsoft-com:vml";
inline constexpr char16_t O[] = u"urn:schemas-microsoft-com:office:office";
inline constexpr char16_t W10[] = u"urn:schemas-microsoft-com:office:word";
inline constexpr char16_t XML[] = u"http://www.w3.org/XML/1998/namespace";

// Microsoft extension vocabularies Word/PowerPoint write by default.
inline constexpr char16_t WPS[] = u"http://schemas.microsoft.com/office/word/2010/wordprocessingShape";
inline constexpr char16_t WPG[] = u"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup";
inline constexpr char16_t WPC[] = u"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas";
inline constexpr char16_t WPI[] = u"http://schemas.microsoft.com/office/word/2010/wordprocessingInk";
inline constexpr char16_t WP14[] = u"http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing";
inline constexpr char16_t W14[] = u"http://schemas.microsoft.com/office/word/2010/wordml";
inline constexpr char16_t W15[] = u"http://schemas.microsoft.com/office/word/2012/wordml";
inline constexpr char16_t A14[] = u"http://schemas.microsoft.com/office/drawing/2010/main";
inline constexpr char16_t P14[] = u"http://schemas.microsoft.com/office/powerpoint/2010/main";

// Microsoft Visio 2013+ (VSDX, [MS-VSDX]) and custom document properties.
inline constexpr char16_t VISIO[] = u"http://schemas.microsoft.com/office/visio/2012/main";
inline constexpr char16_t CUSTOM_PROPERTIES[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/custom-properties";

// Package-level vocabularies.
inline constexpr char16_t CT[] = u"http://schemas.openxmlformats.org/package/2006/content-types";
inline constexpr char16_t RELS[] = u"http://schemas.openxmlformats.org/package/2006/relationships";
inline constexpr char16_t CP[] = u"http://schemas.openxmlformats.org/package/2006/metadata/core-properties";
inline constexpr char16_t DC[] = u"http://purl.org/dc/elements/1.1/";
inline constexpr char16_t DCTERMS[] = u"http://purl.org/dc/terms/";
inline constexpr char16_t DCMITYPE[] = u"http://purl.org/dc/dcmitype/";
inline constexpr char16_t XSI[] = u"http://www.w3.org/2001/XMLSchema-instance";
inline constexpr char16_t EP[] = u"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties";
inline constexpr char16_t VT[] = u"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes";

// The conventional prefix for `uri`, or an empty string when the URI is
// not one the suite knows (the writer then invents ns0, ns1, …).
[[nodiscard]] QString conventionalPrefix(QStringView uri);

// True for vocabularies Office writes as the DEFAULT namespace (no prefix):
// [Content_Types].xml, the .rels parts, docProps/app.xml and custom.xml,
// and every Visio part (libvisio, LibreOffice's Visio reader, matches
// element names literally: <ns0:Shape> is not a shape to it).
// AGENT-GUARD: LibreOffice refuses a package whose relationships part is
// <ns0:Relationships xmlns:ns0="…"> ("source file could not be loaded") —
// legal XML, but OPC consumers match these roots literally.
[[nodiscard]] bool isDefaultNamespace(QStringView uri);

// Strict (ISO/IEC 29500 Strict) URIs mapped to their Transitional twins;
// any other URI is returned unchanged.
[[nodiscard]] QString normalizeNamespace(QStringView uri);

} // namespace QindaOffice::Ooxml::Ns

namespace QindaOffice::Ooxml::RelType {

inline constexpr char16_t OfficeDocument[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
inline constexpr char16_t CoreProperties[] =
    u"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties";
inline constexpr char16_t ExtendedProperties[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties";
inline constexpr char16_t Thumbnail[] =
    u"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";
inline constexpr char16_t Styles[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";
inline constexpr char16_t Numbering[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering";
inline constexpr char16_t Settings[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings";
inline constexpr char16_t WebSettings[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/webSettings";
inline constexpr char16_t FontTable[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/fontTable";
inline constexpr char16_t Theme[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme";
inline constexpr char16_t Header[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header";
inline constexpr char16_t Footer[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer";
inline constexpr char16_t Footnotes[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes";
inline constexpr char16_t Endnotes[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes";
inline constexpr char16_t Comments[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
inline constexpr char16_t Image[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
inline constexpr char16_t Hyperlink[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
inline constexpr char16_t Chart[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart";
inline constexpr char16_t Slide[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide";
inline constexpr char16_t SlideLayout[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout";
inline constexpr char16_t SlideMaster[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster";
inline constexpr char16_t NotesSlide[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide";
inline constexpr char16_t NotesMaster[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster";
inline constexpr char16_t PresProps[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/presProps";
inline constexpr char16_t ViewProps[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/viewProps";
inline constexpr char16_t TableStyles[] =
    u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/tableStyles";

// Strict relationship types mapped to Transitional; others unchanged.
[[nodiscard]] QString normalizeRelationshipType(QStringView type);

} // namespace QindaOffice::Ooxml::RelType

namespace QindaOffice::Ooxml::ContentType {

inline constexpr char16_t Relationships[] = u"application/vnd.openxmlformats-package.relationships+xml";
inline constexpr char16_t Xml[] = u"application/xml";
inline constexpr char16_t CoreProperties[] = u"application/vnd.openxmlformats-package.core-properties+xml";
inline constexpr char16_t ExtendedProperties[] =
    u"application/vnd.openxmlformats-officedocument.extended-properties+xml";
inline constexpr char16_t Theme[] = u"application/vnd.openxmlformats-officedocument.theme+xml";

// The image content type for a file extension ("png" → "image/png"), or
// an empty string when the extension is not an image format Office uses.
[[nodiscard]] QString forImageExtension(QStringView extension);

} // namespace QindaOffice::Ooxml::ContentType
