// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A PowerPoint package built by hand for the pptx gates (M62, D-070):
// a theme, one master (title/body placeholders, txStyles with bullets, a
// decorative "logo"), one layout (title inherited, body moved and restyled
// by its lstStyle), and five slides —
//   1. placeholders inheriting from layout and master, a text box in a
//      theme colour with lumMod, auto-numbered paragraphs, notes, a build;
//   2. a table with a merge (PowerPoint's default style, undefined here),
//      a fade transition;
//   3. a cropped picture and a nested group;
//   4. a chart with cached values;
//   5. hidden, with an unknown graphic frame and a gradient-filled shape.
// Everything a test asserts on is spelled out in the XML below.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QBuffer>
#include <QImage>

namespace PptxFixture {

namespace Ooxml = QindaOffice::Ooxml;

inline const char *const kNs =
    R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main")";

inline QByteArray xml(const char *body)
{
    QByteArray out("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    out += QByteArray(body).replace("NS", kNs);
    return out;
}

inline QByteArray pngBytes(const QColor &color, const QSize &size = QSize(40, 20))
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// The unknown graphic frame of slide 5, as the test compares it.
inline const char *const kUnknownFrame =
    R"(<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="7" name="Vendor Widget"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr><p:xfrm><a:off x="1270000" y="1270000"/><a:ext cx="2540000" cy="1270000"/></p:xfrm><a:graphic><a:graphicData uri="urn:example:widget"><w:widget xmlns:w="urn:example:widget" level="3"><w:dial value="42"/></w:widget></a:graphicData></a:graphic></p:graphicFrame>)";

// presentation.xml, the theme, the master, the layout and the notes master.
inline void addSkeleton(QindaOffice::Ooxml::PackageWriter &w)
{
    using namespace QindaOffice::Ooxml;
    const QString pml = QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.");
    const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
    w.addPart(QStringLiteral("ppt/presentation.xml"), xml(R"(<p:presentation NS saveSubsetFonts="1">
<p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>
<p:notesMasterIdLst><p:notesMasterId r:id="rId8"/></p:notesMasterIdLst>
<p:sldIdLst><p:sldId id="256" r:id="rId2"/><p:sldId id="257" r:id="rId3"/><p:sldId id="258" r:id="rId4"/><p:sldId id="259" r:id="rId5"/><p:sldId id="260" r:id="rId6"/></p:sldIdLst>
<p:sldSz cx="12192000" cy="6858000"/><p:notesSz cx="6858000" cy="9144000"/>
<p:defaultTextStyle><a:lvl1pPr marL="0" algn="l"><a:defRPr sz="1800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:defaultTextStyle>
</p:presentation>)"),
              pml + QStringLiteral("presentation.main+xml"));
    w.addRelationshipWithId(QString(), QStringLiteral("rId1"), QString::fromUtf16(RelType::OfficeDocument),
                            QStringLiteral("ppt/presentation.xml"));
    const QString pres = QStringLiteral("ppt/presentation.xml");
    w.addRelationshipWithId(pres, QStringLiteral("rId1"), rel + QStringLiteral("slideMaster"),
                            QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    for (int i = 1; i <= 5; ++i) {
        w.addRelationshipWithId(pres, QStringLiteral("rId%1").arg(i + 1), rel + QStringLiteral("slide"),
                                QStringLiteral("ppt/slides/slide%1.xml").arg(i));
    }
    w.addRelationshipWithId(pres, QStringLiteral("rId7"), rel + QStringLiteral("theme"),
                            QStringLiteral("ppt/theme/theme1.xml"));
    w.addRelationshipWithId(pres, QStringLiteral("rId8"), rel + QStringLiteral("notesMaster"),
                            QStringLiteral("ppt/notesMasters/notesMaster1.xml"));

    w.addPart(QStringLiteral("ppt/theme/theme1.xml"), xml(R"(<a:theme NS name="Fixture Theme"><a:themeElements>
<a:clrScheme name="Office"><a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1><a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="44546A"/></a:dk2><a:lt2><a:srgbClr val="E7E6E6"/></a:lt2><a:accent1><a:srgbClr val="4472C4"/></a:accent1><a:accent2><a:srgbClr val="ED7D31"/></a:accent2><a:accent3><a:srgbClr val="A5A5A5"/></a:accent3><a:accent4><a:srgbClr val="FFC000"/></a:accent4><a:accent5><a:srgbClr val="5B9BD5"/></a:accent5><a:accent6><a:srgbClr val="70AD47"/></a:accent6><a:hlink><a:srgbClr val="0563C1"/></a:hlink><a:folHlink><a:srgbClr val="954F72"/></a:folHlink></a:clrScheme>
<a:fontScheme name="Office"><a:majorFont><a:latin typeface="Calibri Light"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont><a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme>
<a:fmtScheme name="Office"><a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"><a:tint val="50000"/></a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val="phClr"><a:shade val="80000"/></a:schemeClr></a:solidFill></a:fillStyleLst>
<a:lnStyleLst><a:ln w="6350"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="12700"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="19050"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln></a:lnStyleLst>
<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>
<a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"><a:tint val="95000"/></a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val="phClr"><a:shade val="90000"/></a:schemeClr></a:solidFill></a:bgFillStyleLst></a:fmtScheme>
</a:themeElements></a:theme>)"),
              QString::fromUtf16(ContentType::Theme));

    // The master: title at (36, 18) 888x72 pt, body at (36, 108) 888x360.
    w.addPart(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), xml(R"(<p:sldMaster NS><p:cSld name="Fixture Master"><p:bg><p:bgPr><a:solidFill><a:srgbClr val="FFFFF0"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>
<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>
<p:sp><p:nvSpPr><p:cNvPr id="2" name="Title Placeholder 1"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x="457200" y="228600"/><a:ext cx="11277600" cy="914400"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr><p:txBody><a:bodyPr anchor="ctr"/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Master title</a:t></a:r></a:p></p:txBody></p:sp>
<p:sp><p:nvSpPr><p:cNvPr id="3" name="Text Placeholder 2"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="body" idx="1"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x="457200" y="1371600"/><a:ext cx="11277600" cy="4572000"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Master text</a:t></a:r></a:p></p:txBody></p:sp>
<p:sp><p:nvSpPr><p:cNvPr id="4" name="Logo"/><p:cNvSpPr/><p:nvPr userDrawn="1"/></p:nvSpPr><p:spPr><a:xfrm><a:off x="11430000" y="6350000"/><a:ext cx="508000" cy="254000"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:solidFill><a:schemeClr val="accent6"/></a:solidFill><a:ln><a:noFill/></a:ln></p:spPr></p:sp>
</p:spTree></p:cSld>
<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>
<p:txStyles><p:titleStyle><a:lvl1pPr algn="l"><a:defRPr sz="4400" b="0"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr></p:titleStyle>
<p:bodyStyle><a:lvl1pPr marL="228600" indent="-228600"><a:spcBef><a:spcPts val="1000"/></a:spcBef><a:buFont typeface="Arial"/><a:buChar char="&#x2022;"/><a:defRPr sz="2800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr>
<a:lvl2pPr marL="685800" indent="-228600"><a:buFont typeface="Arial"/><a:buChar char="&#x2013;"/><a:defRPr sz="2400"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl2pPr></p:bodyStyle>
<p:otherStyle><a:lvl1pPr><a:defRPr sz="1800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:otherStyle></p:txStyles>
</p:sldMaster>)"),
              pml + QStringLiteral("slideMaster+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), QStringLiteral("rId1"),
                            rel + QStringLiteral("slideLayout"), QStringLiteral("ppt/slideLayouts/slideLayout1.xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), QStringLiteral("rId2"),
                            rel + QStringLiteral("theme"), QStringLiteral("ppt/theme/theme1.xml"));

    // The layout: the title as the master has it, the body moved to
    // (72, 144) 432x288 pt and restyled to 20 pt accent2 at level 1.
    w.addPart(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), xml(R"(<p:sldLayout NS type="obj" preserve="1"><p:cSld name="Title and Content"><p:spTree>
<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>
<p:sp><p:nvSpPr><p:cNvPr id="2" name="Title 1"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang="en-US"/></a:p></p:txBody></p:sp>
<p:sp><p:nvSpPr><p:cNvPr id="3" name="Content Placeholder 2"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph idx="1"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x="914400" y="1828800"/><a:ext cx="5486400" cy="3657600"/></a:xfrm></p:spPr><p:txBody><a:bodyPr/><a:lstStyle><a:lvl1pPr><a:defRPr sz="2000"><a:solidFill><a:schemeClr val="accent2"/></a:solidFill></a:defRPr></a:lvl1pPr></a:lstStyle><a:p><a:endParaRPr lang="en-US"/></a:p></p:txBody></p:sp>
</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>)"),
              pml + QStringLiteral("slideLayout+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), QStringLiteral("rId1"),
                            rel + QStringLiteral("slideMaster"), QStringLiteral("ppt/slideMasters/slideMaster1.xml"));

    w.addPart(QStringLiteral("ppt/theme/theme2.xml"), w.partData(QStringLiteral("ppt/theme/theme1.xml")),
              QString::fromUtf16(ContentType::Theme));
    w.addPart(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), xml(R"(<p:notesMaster NS><p:cSld><p:spTree>
<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>
<p:sp><p:nvSpPr><p:cNvPr id="2" name="Notes Placeholder 1"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="body" sz="quarter" idx="1"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x="685800" y="4400550"/><a:ext cx="5486400" cy="3600450"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang="en-US"/></a:p></p:txBody></p:sp>
</p:spTree></p:cSld><p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/></p:notesMaster>)"),
              pml + QStringLiteral("notesMaster+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), QStringLiteral("rId1"),
                            rel + QStringLiteral("theme"), QStringLiteral("ppt/theme/theme2.xml"));
}

} // namespace PptxFixture
