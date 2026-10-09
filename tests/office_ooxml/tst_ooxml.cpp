// SPDX-License-Identifier: GPL-3.0-or-later
// M60 gates for office_ooxml: the XML tree, the OPC package round trip,
// relationship copying, DrawingML colours and units. M65 (D-073): braced
// values that are not prefix lists, xml:space="preserve", Visio parts.

#include "ooxml_copier.h"
#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_properties.h"
#include "ooxml_theme.h"
#include "ooxml_units.h"
#include "ooxml_xml.h"
#include "support/ooxml_schema.h"

#include <QtTest>

using namespace QindaOffice::Ooxml;

class OoxmlTest : public QObject {
    Q_OBJECT
private slots:
    void parsesNamespacesAndNormalisesStrict();
    void keepsSignificantWhitespaceOnly();
    void rewritesPrefixListsThroughConventionalPrefixes();
    void bracedValuesAreNotPrefixLists();
    void honoursXmlSpacePreserve();
    void visioPartsUseTheDefaultNamespace();
    void keepsBracedGuidAttributes();
    void escapesAndStripsInvalidCharacters();
    void fragmentsRoundTrip();
    void rejectsDtd();
    void packageRoundTrip();
    void zipHasNoDirectoryEntriesAndTypesFirst();
    void packagePartsUseTheDefaultNamespace();
    void relativeTargets();
    void copierCarriesPartsOnceWithTheirRelationships();
    void copierDropsDanglingIds();
    void copierResolvesMappedParts();
    void resolvesThemeColours();
    void highlightNames();
    void units();
    void coreProperties();
    void schemaHelperValidatesAndPreprocesses();
};

void OoxmlTest::parsesNamespacesAndNormalisesStrict()
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<x:document xmlns:x="http://purl.oclc.org/ooxml/wordprocessingml/main">
  <x:body><x:p x:rsidR="00AB"><x:r><x:t>Hi</x:t></x:r></x:p></x:body>
</x:document>)";
    QString error;
    const XmlElementPtr root = parseXml(xml, &error);
    QVERIFY2(root, qPrintable(error));
    const QString w = QString::fromUtf16(Ns::W);
    QVERIFY(root->is(w, u"document"));
    const XmlElement *p = root->child(w, u"body")->child(w, u"p");
    QVERIFY(p != nullptr);
    QCOMPARE(p->attribute(w, u"rsidR"), QStringLiteral("00AB"));
    QCOMPARE(p->text(), QStringLiteral("Hi"));
    // Written back with Word's conventional prefix.
    const QByteArray out = serializeDocument(*root);
    QVERIFY2(out.contains("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""),
             out.constData());
    QVERIFY(out.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"));
}

void OoxmlTest::keepsSignificantWhitespaceOnly()
{
    const QByteArray xml = "<w:p xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
                           "  <w:r>\n    <w:t xml:space=\"preserve\"> </w:t>\n  </w:r>\n</w:p>";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QString w = QString::fromUtf16(Ns::W);
    QCOMPARE(root->children.size(), 1); // indentation dropped
    const XmlElement *t = root->child(w, u"r")->child(w, u"t");
    QCOMPARE(t->text(), QStringLiteral(" ")); // the run's space kept
    QCOMPARE(t->attribute(QString::fromUtf16(Ns::XML), u"space"), QStringLiteral("preserve"));
    QVERIFY(serializeDocument(*root).contains("<w:t xml:space=\"preserve\"> </w:t>"));
}

void OoxmlTest::rewritesPrefixListsThroughConventionalPrefixes()
{
    // A producer with unusual prefixes: mc:Ignorable and Requires must be
    // rewritten to the prefixes the OUTPUT declares.
    const QByteArray xml = R"(<q:document xmlns:q="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:m2="http://schemas.openxmlformats.org/markup-compatibility/2006"
 xmlns:z14="http://schemas.microsoft.com/office/word/2010/wordml"
 xmlns:shp="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"
 m2:Ignorable="z14">
 <m2:AlternateContent><m2:Choice Requires="shp"><q:p/></m2:Choice><m2:Fallback/></m2:AlternateContent>
</q:document>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QByteArray out = serializeDocument(*root);
    QVERIFY2(out.contains("mc:Ignorable=\"w14\""), out.constData());
    QVERIFY2(out.contains("Requires=\"wps\""), out.constData());
    QVERIFY(out.contains("xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\""));
    QVERIFY(out.contains("xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\""));
}

void OoxmlTest::bracedValuesAreNotPrefixLists()
{
    // GUIDs in braces (Visio's UniqueID, DrawingML's a16:creationId) are
    // values, not "{uri}" tokens: written back as they were read.
    const QByteArray xml = R"(<p:sp xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
 xmlns:a16="http://schemas.microsoft.com/office/drawing/2014/main" a16:id="{6B5C1D3E-0000-4000-8000-000000000001}"
 name="{not a namespace}"/>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QByteArray out = serializeDocument(*root);
    QVERIFY2(out.contains("a16:id=\"{6B5C1D3E-0000-4000-8000-000000000001}\""), out.constData());
    QVERIFY2(out.contains("name=\"{not a namespace}\""), out.constData());
    QVERIFY(!out.contains("ns0"));
}

void OoxmlTest::honoursXmlSpacePreserve()
{
    // Visio <Text> is mixed content: the space between two run markers is
    // text, and the part's root declares xml:space="preserve".
    const QByteArray xml = "<Shape xmlns=\"http://schemas.microsoft.com/office/visio/2012/main\" xml:space=\"preserve\">"
                           "<Text><cp IX=\"0\"/>A<cp IX=\"1\"/> <cp IX=\"0\"/>B</Text></Shape>";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const XmlElement *text = root->child(QString::fromUtf16(Ns::VISIO), u"Text");
    QVERIFY(text);
    QCOMPARE(text->text(), QStringLiteral("A B"));
    QVERIFY(serializeFragment(*text).contains(QStringLiteral("<cp IX=\"1\"/> <cp IX=\"0\"/>")));
}

void OoxmlTest::visioPartsUseTheDefaultNamespace()
{
    // libvisio (LibreOffice's Visio reader) matches element names literally.
    XmlElement root(QString::fromUtf16(Ns::VISIO), QStringLiteral("PageContents"));
    XmlElement &rel = root.appendElement(QString::fromUtf16(Ns::VISIO), QStringLiteral("Rel"));
    rel.setAttribute(QString::fromUtf16(Ns::R), QStringLiteral("id"), QStringLiteral("rId1"));
    const QByteArray out = serializeDocument(root);
    QVERIFY2(out.contains("<PageContents xmlns=\"http://schemas.microsoft.com/office/visio/2012/main\""), out.constData());
    QVERIFY(out.contains("<Rel r:id=\"rId1\"/>"));
}

void OoxmlTest::keepsBracedGuidAttributes()
{
    // PowerPoint stamps braced GUIDs on nearly every shape; they are data,
    // not namespace lists, and must survive verbatim.
    const QByteArray xml = R"(<p:cNvPr xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
 xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" id="4" name="Title 3"><a:extLst>
 <a:ext uri="{FF2B5EF4-FFF2-40B4-BE49-F238E27FC236}"><a16:creationId
 xmlns:a16="http://schemas.microsoft.com/office/drawing/2014/main" id="{1A2B3C4D-0000-1111-2222-333344445555}"/>
 </a:ext></a:extLst></p:cNvPr>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QString out = serializeFragment(*root);
    QVERIFY2(out.contains(QStringLiteral("uri=\"{FF2B5EF4-FFF2-40B4-BE49-F238E27FC236}\"")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("id=\"{1A2B3C4D-0000-1111-2222-333344445555}\"")), qPrintable(out));
    QVERIFY2(!out.contains(QStringLiteral("xmlns:ns0")), qPrintable(out));
    const XmlElementPtr back = parseFragment(out);
    QVERIFY(back);
    QCOMPARE(serializeFragment(*back), out);
}

void OoxmlTest::escapesAndStripsInvalidCharacters()
{
    XmlElement e(QString::fromUtf16(Ns::W), QStringLiteral("t"));
    e.setAttribute(QString(), QStringLiteral("v"), QStringLiteral("a\"b<c&\n"));
    e.appendText(QStringLiteral("x<y & z") + QChar(0x0B) + QChar(0x0001) + QStringLiteral("\tend"));
    const QString out = serializeFragment(e);
    QVERIFY2(out.contains(QStringLiteral("v=\"a&quot;b&lt;c&amp;&#10;\"")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral(">x&lt;y &amp; z\tend<")), qPrintable(out));
    QCOMPARE(xmlSafeText(QString(QChar(0xD800)) + QStringLiteral("ok")), QStringLiteral("ok"));
}

void OoxmlTest::fragmentsRoundTrip()
{
    const QByteArray xml = R"(<w:r xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
 xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
 xmlns:custom="urn:example:vendor"><w:drawing><wp:anchor custom:flag="1" r:id="rId4"/></w:drawing></w:r>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QString fragment = serializeFragment(*root);
    const XmlElementPtr back = parseFragment(fragment);
    QVERIFY(back);
    QCOMPARE(serializeFragment(*back), fragment);
    const XmlElement *anchor = back->descendant(QString::fromUtf16(Ns::WP), u"anchor");
    QVERIFY(anchor != nullptr);
    QCOMPARE(anchor->attribute(u"urn:example:vendor", u"flag"), QStringLiteral("1"));
    QCOMPARE(anchor->attribute(QString::fromUtf16(Ns::R), u"id"), QStringLiteral("rId4"));
}

void OoxmlTest::rejectsDtd()
{
    const QByteArray xml = "<!DOCTYPE a [<!ENTITY x \"yy\">]><a>&x;</a>";
    QString error;
    QVERIFY(!parseXml(xml, &error));
    QVERIFY(!error.isEmpty());
}

static QByteArray samplePackage()
{
    PackageWriter writer;
    XmlElement doc(QString::fromUtf16(Ns::W), QStringLiteral("document"));
    doc.appendElement(doc.ns, QStringLiteral("body"));
    writer.addXmlPart(QStringLiteral("word/document.xml"), doc,
                      QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
    writer.addRelationship(QString(), QString::fromUtf16(RelType::OfficeDocument), QStringLiteral("word/document.xml"));
    writer.addPart(QStringLiteral("word/media/image1.png"), QByteArray("\x89PNG fake", 9));
    const QString imageId = writer.addRelationship(QStringLiteral("word/document.xml"),
                                                   QString::fromUtf16(RelType::Image),
                                                   QStringLiteral("word/media/image1.png"));
    Q_UNUSED(imageId);
    writer.addRelationship(QStringLiteral("word/document.xml"), QString::fromUtf16(RelType::Hyperlink),
                           QStringLiteral("https://example.org/a b"), true);
    // A chart with its own relationship to an embedded workbook.
    writer.addPart(QStringLiteral("word/charts/chart1.xml"), QByteArray("<c:chartSpace/>"),
                   QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"));
    writer.addRelationshipWithId(QStringLiteral("word/charts/chart1.xml"), QStringLiteral("rId3"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/package"),
                                 QStringLiteral("word/embeddings/Book1.xlsx"));
    writer.addPart(QStringLiteral("word/embeddings/Book1.xlsx"), QByteArray("PK fake"),
                   QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"));
    writer.addRelationshipWithId(QStringLiteral("word/document.xml"), QStringLiteral("rId9"),
                                 QString::fromUtf16(RelType::Chart), QStringLiteral("word/charts/chart1.xml"));
    QString error;
    const QByteArray zip = writer.toZip(&error);
    if (zip.isEmpty()) {
        qWarning() << error;
    }
    return zip;
}

void OoxmlTest::packageRoundTrip()
{
    const QByteArray zip = samplePackage();
    QVERIFY(!zip.isEmpty());
    QString error;
    const auto package = Package::fromData(zip, &error);
    QVERIFY2(package, qPrintable(error));
    QCOMPARE(package->mainPart(), QStringLiteral("word/document.xml"));
    QCOMPARE(package->contentType(QStringLiteral("/word/media/image1.png")), QStringLiteral("image/png"));
    QCOMPARE(package->contentType(QStringLiteral("word/document.xml")),
             QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
    QVERIFY(package->contains(QStringLiteral("WORD/Document.XML"))); // case-insensitive
    const QList<Relationship> rels = package->relationships(QStringLiteral("word/document.xml"));
    QCOMPARE(rels.size(), 3);
    const auto image = package->firstOfType(QStringLiteral("word/document.xml"), QString::fromUtf16(RelType::Image));
    QVERIFY(image);
    QCOMPARE(image->target, QStringLiteral("media/image1.png")); // written relative
    QCOMPARE(package->targetPart(QStringLiteral("word/document.xml"), *image), QStringLiteral("word/media/image1.png"));
    const auto link = package->firstOfType(QStringLiteral("word/document.xml"), QString::fromUtf16(RelType::Hyperlink));
    QVERIFY(link && link->external);
    QCOMPARE(link->target, QStringLiteral("https://example.org/a b"));
    QCOMPARE(package->data(QStringLiteral("word/media/image1.png")), QByteArray("\x89PNG fake", 9));
    // Not a package at all.
    QVERIFY(!Package::fromData(QByteArray("plain text"), &error));
}

void OoxmlTest::zipHasNoDirectoryEntriesAndTypesFirst()
{
    const QByteArray zip = samplePackage();
    // Local file headers: signature PK\3\4, name length at 26, name at 30.
    QStringList names;
    for (qsizetype at = zip.indexOf("PK\x03\x04"); at >= 0; at = zip.indexOf("PK\x03\x04", at + 4)) {
        if (at + 30 > zip.size()) {
            break;
        }
        const auto len = quint16(uchar(zip.at(at + 26)) | (uchar(zip.at(at + 27)) << 8));
        names.append(QString::fromUtf8(zip.mid(at + 30, len)));
    }
    QVERIFY(!names.isEmpty());
    QCOMPARE(names.first(), QStringLiteral("[Content_Types].xml"));
    for (const QString &name : std::as_const(names)) {
        QVERIFY2(!name.endsWith(QLatin1Char('/')), qPrintable(name));
    }
}

void OoxmlTest::packagePartsUseTheDefaultNamespace()
{
    // AGENT-GUARD regression: LibreOffice refused <ns0:Relationships …>.
    const auto package = Package::fromData(samplePackage());
    QVERIFY(package);
    QVERIFY(package->data(QStringLiteral("_rels/.rels"))
                .contains("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"));
    QVERIFY(package->data(QStringLiteral("[Content_Types].xml"))
                .contains("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"));
    PackageWriter writer;
    writeProperties(writer, {}, QStringLiteral("Test"));
    QVERIFY(writer.partData(QStringLiteral("docProps/app.xml")).contains("<Properties xmlns=\""));
}

void OoxmlTest::relativeTargets()
{
    QCOMPARE(relativeTargetFor(QStringLiteral("word/document.xml"), QStringLiteral("word/media/a.png")),
             QStringLiteral("media/a.png"));
    QCOMPARE(relativeTargetFor(QStringLiteral("ppt/slides/slide1.xml"), QStringLiteral("ppt/media/a.png")),
             QStringLiteral("../media/a.png"));
    QCOMPARE(relativeTargetFor(QString(), QStringLiteral("word/document.xml")), QStringLiteral("word/document.xml"));
    QCOMPARE(Package::resolveTarget(QStringLiteral("ppt/slides/slide1.xml"), QStringLiteral("../media/a.png")),
             QStringLiteral("ppt/media/a.png"));
    QCOMPARE(Package::resolveTarget(QStringLiteral("xl/workbook.xml"), QStringLiteral("/xl/worksheets/sheet1.xml")),
             QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(relationshipsPartFor(QStringLiteral("word/header1.xml")), QStringLiteral("word/_rels/header1.xml.rels"));
}

void OoxmlTest::copierCarriesPartsOnceWithTheirRelationships()
{
    const auto source = Package::fromData(samplePackage());
    QVERIFY(source);
    PackageWriter writer;
    writer.addPart(QStringLiteral("word/media/image1.png"), QByteArray("other image")); // name collision
    RelationshipCopier copier(source, &writer);
    const QString r = QString::fromUtf16(Ns::R);
    const auto image = source->firstOfType(QStringLiteral("word/document.xml"), QString::fromUtf16(RelType::Image));
    XmlElement blip(QString::fromUtf16(Ns::A), QStringLiteral("blip"));
    blip.setAttribute(r, QStringLiteral("embed"), image->id);
    XmlElement chart(QString::fromUtf16(Ns::C), QStringLiteral("chart"));
    chart.setAttribute(r, QStringLiteral("id"), QStringLiteral("rId9"));
    XmlElement again = blip;
    copier.rewrite(blip, QStringLiteral("word/document.xml"), QStringLiteral("word/header1.xml"));
    copier.rewrite(again, QStringLiteral("word/document.xml"), QStringLiteral("word/header1.xml"));
    copier.rewrite(chart, QStringLiteral("word/document.xml"), QStringLiteral("word/header1.xml"));
    const QString newImageId = blip.attribute(r, u"embed");
    QCOMPARE(again.attribute(r, u"embed"), newImageId); // memoised: one relationship
    const QList<Relationship> rels = writer.relationshipsOf(QStringLiteral("word/header1.xml"));
    QCOMPARE(rels.size(), 2);
    QString copiedImage;
    for (const Relationship &rel : rels) {
        if (rel.id == newImageId) {
            copiedImage = rel.target;
        }
    }
    QCOMPARE(copiedImage, QStringLiteral("word/media/image2.png")); // collision avoided
    QCOMPARE(writer.partData(copiedImage), QByteArray("\x89PNG fake", 9));
    QCOMPARE(writer.partData(QStringLiteral("word/media/image1.png")), QByteArray("other image"));
    // The chart's own relationship keeps its id; the embedded workbook came along.
    const QList<Relationship> chartRels = writer.relationshipsOf(QStringLiteral("word/charts/chart1.xml"));
    QCOMPARE(chartRels.size(), 1);
    QCOMPARE(chartRels.first().id, QStringLiteral("rId3"));
    QVERIFY(writer.hasPart(QStringLiteral("word/embeddings/Book1.xlsx")));
    QCOMPARE(writer.contentTypeOf(QStringLiteral("word/charts/chart1.xml")),
             QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"));
}

void OoxmlTest::copierDropsDanglingIds()
{
    const auto source = Package::fromData(samplePackage());
    PackageWriter writer;
    RelationshipCopier copier(source, &writer);
    XmlElement blip(QString::fromUtf16(Ns::A), QStringLiteral("blip"));
    blip.setAttribute(QString::fromUtf16(Ns::R), QStringLiteral("embed"), QStringLiteral("rId404"));
    copier.rewrite(blip, QStringLiteral("word/document.xml"), QStringLiteral("word/document.xml"));
    QVERIFY(!blip.hasAttribute(QString::fromUtf16(Ns::R), u"embed"));
}

void OoxmlTest::copierResolvesMappedParts()
{
    // A part the caller regenerates (a slide) is mapped, not copied: a
    // copied part that links to it gets the caller's part.
    PackageWriter source;
    source.addPart(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), QByteArray("<m/>"), QStringLiteral("text/xml"));
    source.addPart(QStringLiteral("ppt/slides/slide1.xml"), QByteArray("<old/>"), QStringLiteral("text/xml"));
    const QString slideType = QString::fromUtf16(RelType::Slide);
    source.addRelationshipWithId(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), QStringLiteral("rId3"), slideType,
                                 QStringLiteral("ppt/slides/slide1.xml"));
    source.addRelationshipWithId(QString(), QStringLiteral("rId1"), QString::fromUtf16(RelType::OfficeDocument),
                                 QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    const auto package = Package::fromData(source.toZip());
    QVERIFY(package);
    PackageWriter out;
    out.addPart(QStringLiteral("ppt/slides/slide7.xml"), QByteArray("<new/>"), QStringLiteral("text/xml"));
    RelationshipCopier copier(package, &out);
    copier.mapPart(QStringLiteral("ppt/slides/slide1.xml"), QStringLiteral("ppt/slides/slide7.xml"));
    QCOMPARE(copier.copyPart(QStringLiteral("ppt/slideMasters/slideMaster1.xml")),
             QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    QVERIFY(!out.hasPart(QStringLiteral("ppt/slides/slide1.xml")));
    const QList<Relationship> rels = out.relationshipsOf(QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    QCOMPARE(rels.size(), 1);
    QCOMPARE(rels.first().id, QStringLiteral("rId3"));
    QCOMPARE(rels.first().target, QStringLiteral("ppt/slides/slide7.xml"));
}

void OoxmlTest::resolvesThemeColours()
{
    const QByteArray xml = R"(<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="T">
 <a:themeElements><a:clrScheme name="c">
  <a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1><a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1>
  <a:dk2><a:srgbClr val="44546A"/></a:dk2><a:lt2><a:srgbClr val="E7E6E6"/></a:lt2>
  <a:accent1><a:srgbClr val="4472C4"/></a:accent1>
 </a:clrScheme><a:fontScheme name="f"><a:majorFont><a:latin typeface="Aptos Display"/></a:majorFont>
 <a:minorFont><a:latin typeface="Aptos"/></a:minorFont></a:fontScheme></a:themeElements></a:theme>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const Theme theme = parseTheme(*root);
    QCOMPARE(theme.minorLatin, QStringLiteral("Aptos"));
    QCOMPARE(theme.majorLatin, QStringLiteral("Aptos Display"));
    QCOMPARE(theme.colors.value(QStringLiteral("accent1")), QColor(0x44, 0x72, 0xC4));
    // tx1 maps to dk1 through the default colour map.
    const XmlElementPtr tx1 = parseXml(R"(<a:schemeClr xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" val="tx1"/>)");
    QCOMPARE(resolveColor(tx1.get(), theme), QColor(Qt::black));
    // accent1 "Lighter 40%": lumMod 60000 + lumOff 40000 → Office's 8FAADC.
    const XmlElementPtr lighter = parseXml(R"(<a:schemeClr xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" val="accent1"><a:lumMod val="60000"/><a:lumOff val="40000"/></a:schemeClr>)");
    const QColor c = resolveColor(lighter.get(), theme);
    QVERIFY2(qAbs(c.red() - 0x8F) <= 2 && qAbs(c.green() - 0xAA) <= 2 && qAbs(c.blue() - 0xDC) <= 2,
             qPrintable(c.name()));
    // Alpha.
    const XmlElementPtr alpha = parseXml(R"(<a:srgbClr xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" val="FF0000"><a:alpha val="50000"/></a:srgbClr>)");
    QCOMPARE(resolveColor(alpha.get(), theme).alpha(), 128);
    QCOMPARE(applyShade(QColor(Qt::white), 0.0), QColor(Qt::black));
    QCOMPARE(applyTint(QColor(Qt::black), 0.0), QColor(Qt::white));
}

void OoxmlTest::highlightNames()
{
    QCOMPARE(highlightColor(QStringLiteral("yellow")), QColor(Qt::yellow));
    QCOMPARE(nearestHighlightName(QColor(250, 250, 10)), QStringLiteral("yellow"));
    QCOMPARE(nearestHighlightName(QColor(0, 0, 120)), QStringLiteral("darkBlue"));
    QVERIFY(nearestHighlightName(QColor(Qt::transparent)).isEmpty());
}

void OoxmlTest::units()
{
    QCOMPARE(Units::twipsToPx(1440), 96.0);
    QCOMPARE(Units::emuToPx(914400), 96.0);
    QCOMPARE(Units::parseTwips(QStringLiteral("1in")), 1440.0);
    QCOMPARE(Units::parseTwips(QStringLiteral("720")), 720.0);
    QCOMPARE(Units::parseTwips(QStringLiteral("2.54cm")), 1440.0);
    QVERIFY(Units::parseOnOff(QString()));
    QVERIFY(!Units::parseOnOff(QStringLiteral("0")));
    QVERIFY(!Units::parseOnOff(QStringLiteral("false")));
}

void OoxmlTest::coreProperties()
{
    PackageWriter writer;
    CoreProperties props;
    props.title = QStringLiteral("Quarterly <Report>");
    props.creator = QStringLiteral("Jordan");
    props.modified = QDateTime(QDate(2026, 10, 8), QTime(12, 0), QTimeZone::UTC);
    writeProperties(writer, props, QStringLiteral("QindaWrite"), 3, 120);
    writer.addPart(QStringLiteral("x.xml"), QByteArray("<x/>"));
    const auto package = Package::fromData(writer.toZip());
    QVERIFY(package);
    const CoreProperties back = readCoreProperties(*package);
    QCOMPARE(back.title, props.title);
    QCOMPARE(back.creator, props.creator);
    QCOMPARE(back.modified, props.modified);
    QVERIFY(package->data(QStringLiteral("docProps/app.xml")).contains("<ep:Application>QindaWrite</ep:Application>")
            || package->data(QStringLiteral("docProps/app.xml")).contains("Application>QindaWrite<"));
}

void OoxmlTest::schemaHelperValidatesAndPreprocesses()
{
    using namespace QindaOffice::Test;
    if (!schemaValidationAvailable()) {
        QSKIP("QQO_OOXML_SCHEMA_DIR or xmllint unavailable");
    }
    // Valid once Markup Compatibility drops the ignorable w14 attribute and
    // resolves the AlternateContent to its Fallback.
    const QByteArray good = R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
 xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml" mc:Ignorable="w14">
 <w:body><w:p w14:paraId="1A2B3C4D"><w:pPr><w:spacing w:after="0"/><w:jc w:val="center"/></w:pPr>
 <mc:AlternateContent><mc:Choice Requires="w14"><w:r><w14:foo/></w:r></mc:Choice>
 <mc:Fallback><w:r><w:t>x</w:t></w:r></mc:Fallback></mc:AlternateContent></w:p><w:sectPr/></w:body></w:document>)";
    // jc before spacing breaks the schema's sequence.
    const QByteArray bad = R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
 <w:body><w:p><w:pPr><w:jc w:val="center"/><w:spacing w:after="0"/></w:pPr></w:p></w:body></w:document>)";
    PackageWriter writer;
    writer.addPart(QStringLiteral("word/document.xml"), good);
    auto package = Package::fromData(writer.toZip());
    QVERIFY(package);
    QCOMPARE(validatePackage(*package), QStringList());
    writer.addPart(QStringLiteral("word/document.xml"), bad);
    package = Package::fromData(writer.toZip());
    const QStringList errors = validatePackage(*package);
    QCOMPARE(errors.size(), 1);
    QVERIFY2(errors.first().contains(QStringLiteral("spacing")), qPrintable(errors.first()));
}

QTEST_GUILESS_MAIN(OoxmlTest)
#include "tst_ooxml.moc"
