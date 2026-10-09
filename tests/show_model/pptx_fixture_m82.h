// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M82 (D-091) additions to the hand-built PowerPoint package
// (pptx_fixture_m75.h): three more layouts on its master — Title Slide,
// Two Content and Blank, each with the date, footer and slide number — so
// New Slide, Layout and Reset have something to choose from; slide 1 with
// a slide number (the footers a new slide after it takes); and a template
// (.potx) of its own: another theme (Georgia / Verdana, a red accent), a
// master named "Template Master" and two layouts placed elsewhere than the
// deck's. Everything a test asserts on is spelled out here.

#include "pptx_fixture_m75.h"

namespace PptxFixture::M82 {

inline QString pml(const char *kind)
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.") + QLatin1String(kind);
}

inline QString relType(const char *kind)
{
    return QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + QLatin1String(kind);
}

// A placeholder as a layout spells one: p:ph, a position (EMU) or none.
inline QByteArray placeholder(int id, const char *name, const char *ph, const char *xfrm = nullptr)
{
    QByteArray out = "<p:sp><p:nvSpPr><p:cNvPr id=\"" + QByteArray::number(id) + "\" name=\"" + name
        + "\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>" + ph + "</p:nvPr></p:nvSpPr><p:spPr>";
    if (xfrm != nullptr) {
        out += xfrm;
    }
    return out + "</p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang=\"en-US\"/></a:p></p:txBody></p:sp>";
}

inline QByteArray xfrm(qint64 x, qint64 y, qint64 cx, qint64 cy)
{
    return "<a:xfrm><a:off x=\"" + QByteArray::number(x) + "\" y=\"" + QByteArray::number(y) + "\"/><a:ext cx=\""
        + QByteArray::number(cx) + "\" cy=\"" + QByteArray::number(cy) + "\"/></a:xfrm>";
}

// The date (36, 500), footer (330, 500) and slide number (700, 500), each
// 30 pt high.
inline QByteArray footers(int firstId)
{
    return placeholder(firstId, "Date Placeholder", "<p:ph type=\"dt\" sz=\"half\" idx=\"10\"/>",
                       xfrm(457200, 6350000, 2540000, 381000).constData())
        + placeholder(firstId + 1, "Footer Placeholder", "<p:ph type=\"ftr\" sz=\"quarter\" idx=\"11\"/>",
                      xfrm(4191000, 6350000, 3810000, 381000).constData())
        + placeholder(firstId + 2, "Slide Number Placeholder", "<p:ph type=\"sldNum\" sz=\"quarter\" idx=\"12\"/>",
                      xfrm(8890000, 6350000, 2540000, 381000).constData());
}

inline QByteArray layoutPart(const char *type, const char *name, const QByteArray &shapes)
{
    return xml((QByteArray("<p:sldLayout NS type=\"") + type + "\"><p:cSld name=\"" + name
                + "\"><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
                + shapes + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>")
                   .constData());
}

// Title Slide: the centred title at (72, 120) 816x120 pt, the subtitle at
// (144, 270) 672x90 pt.
inline QByteArray titleSlideLayout()
{
    return layoutPart("title", "Title Slide",
                      placeholder(2, "Title 1", "<p:ph type=\"ctrTitle\"/>", xfrm(914400, 1524000, 10363200, 1524000).constData())
                          + placeholder(3, "Subtitle 2", "<p:ph type=\"subTitle\" idx=\"1\"/>",
                                        xfrm(1828800, 3429000, 8534400, 1143000).constData())
                          + footers(4));
}

// Two Content: the master's title, content at (36, 108) and (492, 108),
// each 432x360 pt.
inline QByteArray twoContentLayout()
{
    return layoutPart("twoObj", "Two Content",
                      placeholder(2, "Title 1", "<p:ph type=\"title\"/>")
                          + placeholder(3, "Content Placeholder 2", "<p:ph sz=\"half\" idx=\"1\"/>",
                                        xfrm(457200, 1371600, 5486400, 4572000).constData())
                          + placeholder(4, "Content Placeholder 3", "<p:ph sz=\"half\" idx=\"2\"/>",
                                        xfrm(6248400, 1371600, 5486400, 4572000).constData())
                          + footers(5));
}

inline QByteArray blankLayout()
{
    return layoutPart("blank", "Blank", footers(2));
}

// The base fixture with the three layouts after its own (slideLayout2-4)
// and a slide number on slide 1.
inline QByteArray layoutsDeck()
{
    return edited([](QindaOffice::Ooxml::PackageWriter &w) {
        const QString master = QStringLiteral("ppt/slideMasters/slideMaster1.xml");
        QByteArray masterXml = w.partData(master);
        masterXml.replace(R"(<p:sldLayoutId id="2147483649" r:id="rId1"/>)",
                          R"(<p:sldLayoutId id="2147483649" r:id="rId1"/><p:sldLayoutId id="2147483650" r:id="rId10"/><p:sldLayoutId id="2147483651" r:id="rId11"/><p:sldLayoutId id="2147483652" r:id="rId12"/>)");
        w.addPart(master, masterXml, pml("slideMaster+xml"));
        const QByteArray parts[] = {titleSlideLayout(), twoContentLayout(), blankLayout()};
        for (int i = 0; i < 3; ++i) {
            const QString part = QStringLiteral("ppt/slideLayouts/slideLayout%1.xml").arg(i + 2);
            w.addPart(part, parts[i], pml("slideLayout+xml"));
            w.addRelationshipWithId(master, QStringLiteral("rId%1").arg(10 + i), relType("slideLayout"), part);
            w.addRelationshipWithId(part, QStringLiteral("rId1"), relType("slideMaster"), master);
        }
        const QString slide = QStringLiteral("ppt/slides/slide1.xml");
        QByteArray slideXml = w.partData(slide);
        slideXml.replace("</p:spTree>", R"(<p:sp><p:nvSpPr><p:cNvPr id="9" name="Slide Number Placeholder 8"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="sldNum" sz="quarter" idx="12"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x="8890000" y="6350000"/><a:ext cx="2540000" cy="381000"/></a:xfrm></p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:fld id="{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}" type="slidenum"><a:rPr lang="en-US"/><a:t>1</a:t></a:fld><a:endParaRPr lang="en-US"/></a:p></p:txBody></p:sp></p:spTree>)");
        w.addPart(slide, slideXml, pml("slide+xml"));
    });
}

// A template (.potx): its own theme, "Template Master" (title at (60, 30)
// 840x80 pt, body at (60, 130) 840x340) and two layouts — Title Slide
// (title at (100, 150) 760x120) and Title and Content (content at
// (90, 140) 780x330). No slides.
inline QByteArray templatePackage()
{
    using namespace QindaOffice::Ooxml;
    PackageWriter w;
    w.addPart(QStringLiteral("ppt/presentation.xml"), xml(R"(<p:presentation NS saveSubsetFonts="1">
<p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>
<p:sldSz cx="12192000" cy="6858000"/><p:notesSz cx="6858000" cy="9144000"/>
<p:defaultTextStyle><a:lvl1pPr marL="0" algn="l"><a:defRPr sz="1800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:defaultTextStyle>
</p:presentation>)"),
              pml("template.main+xml"));
    w.addRelationshipWithId(QString(), QStringLiteral("rId1"), QString::fromUtf16(RelType::OfficeDocument),
                            QStringLiteral("ppt/presentation.xml"));
    const QString pres = QStringLiteral("ppt/presentation.xml");
    const QString master = QStringLiteral("ppt/slideMasters/slideMaster1.xml");
    w.addRelationshipWithId(pres, QStringLiteral("rId1"), relType("slideMaster"), master);
    w.addRelationshipWithId(pres, QStringLiteral("rId2"), relType("theme"), QStringLiteral("ppt/theme/theme1.xml"));
    QByteArray theme = build();
    const auto base = Package::fromData(theme);
    QByteArray themeXml = base->data(QStringLiteral("ppt/theme/theme1.xml"));
    themeXml.replace("name=\"Fixture Theme\"", "name=\"Template Theme\"")
        .replace("<a:srgbClr val=\"4472C4\"/>", "<a:srgbClr val=\"C00000\"/>")
        .replace("typeface=\"Calibri Light\"", "typeface=\"Georgia\"")
        .replace("typeface=\"Calibri\"", "typeface=\"Verdana\"");
    w.addPart(QStringLiteral("ppt/theme/theme1.xml"), themeXml, QString::fromUtf16(ContentType::Theme));
    w.addPart(master, xml((QByteArray(R"(<p:sldMaster NS><p:cSld name="Template Master"><p:bg><p:bgPr><a:solidFill><a:srgbClr val="F0F4FF"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>
<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>)")
                          + placeholder(2, "Title Placeholder 1", "<p:ph type=\"title\"/>", xfrm(762000, 381000, 10668000, 1016000).constData())
                          + placeholder(3, "Text Placeholder 2", "<p:ph type=\"body\" idx=\"1\"/>",
                                        xfrm(762000, 1651000, 10668000, 4318000).constData())
                          + R"(</p:spTree></p:cSld>
<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/><p:sldLayoutId id="2147483650" r:id="rId2"/></p:sldLayoutIdLst>
<p:txStyles><p:titleStyle><a:lvl1pPr algn="ctr"><a:defRPr sz="4000"><a:solidFill><a:schemeClr val="accent1"/></a:solidFill><a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr></p:titleStyle>
<p:bodyStyle><a:lvl1pPr marL="228600" indent="-228600"><a:buFont typeface="Arial"/><a:buChar char="&#x2022;"/><a:defRPr sz="2400"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:bodyStyle>
<p:otherStyle><a:lvl1pPr><a:defRPr sz="1800"/></a:lvl1pPr></p:otherStyle></p:txStyles>
</p:sldMaster>)")
                             .constData()),
              pml("slideMaster+xml"));
    const QByteArray layouts[] = {
        layoutPart("title", "Title Slide",
                   placeholder(2, "Title 1", "<p:ph type=\"ctrTitle\"/>", xfrm(1270000, 1905000, 9652000, 1524000).constData())),
        layoutPart("obj", "Title and Content",
                   placeholder(2, "Title 1", "<p:ph type=\"title\"/>")
                       + placeholder(3, "Content Placeholder 2", "<p:ph idx=\"1\"/>",
                                     xfrm(1143000, 1778000, 9906000, 4191000).constData()))};
    for (int i = 0; i < 2; ++i) {
        const QString part = QStringLiteral("ppt/slideLayouts/slideLayout%1.xml").arg(i + 1);
        w.addPart(part, layouts[i], pml("slideLayout+xml"));
        w.addRelationshipWithId(master, QStringLiteral("rId%1").arg(i + 1), relType("slideLayout"), part);
        w.addRelationshipWithId(part, QStringLiteral("rId1"), relType("slideMaster"), master);
    }
    w.addRelationshipWithId(master, QStringLiteral("rId3"), relType("theme"), QStringLiteral("ppt/theme/theme1.xml"));
    return w.toZip();
}

} // namespace PptxFixture::M82
