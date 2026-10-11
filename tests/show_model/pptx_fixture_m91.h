// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M91 (D-099) additions to the hand-built PowerPoint package
// (pptx_fixture_m75.h), spelled the way PowerPoint 365 writes them:
//  - slide 2: action buttons and linked shapes for every Action Settings
//    choice (ids 10–23), linked text (id 24), a transition in
//    mc:AlternateContent with Advance Slide after 5 s and not on a click;
//  - slide 4: Advance Slide after 3 s, and effects with directions —
//    Fly In from the top left, Split horizontal in, Peek In from the
//    right, Float Down, and an exit Wipe from the left;
//  - slide 3: a video with Trim, Fade Duration and two bookmarks;
//  - presentation.xml: two custom shows; presProps.xml: a kiosk show of
//    slides 2–4, looping, with narration, by hand, a blue pen and a green
//    laser, and the extensions PowerPoint adds.
// Everything a test asserts on is spelled out here.

#include "pptx_fixture_m75.h"

namespace PptxFixture::M91 {

namespace Ooxml = QindaOffice::Ooxml;

inline const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
inline const QString kSlideType = QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slide+xml");

inline QByteArray shape(int id, const char *name, const char *preset, int x, const QByteArray &links)
{
    return QStringLiteral(R"(<p:sp><p:nvSpPr><p:cNvPr id="%1" name="%2">%5</p:cNvPr><p:cNvSpPr/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="%3" y="5080000"/><a:ext cx="508000" cy="508000"/></a:xfrm><a:prstGeom prst="%4"><a:avLst/></a:prstGeom><a:solidFill><a:schemeClr val="accent1"/></a:solidFill></p:spPr></p:sp>)")
        .arg(id)
        .arg(QLatin1String(name))
        .arg(x)
        .arg(QLatin1String(preset), QString::fromUtf8(links))
        .toUtf8();
}

inline QByteArray link(const char *rid, const char *action, const QByteArray &extra = QByteArray(), const char *element = "hlinkClick")
{
    QByteArray out = QByteArray("<a:") + element + " r:id=\"" + rid + "\"";
    if (action != nullptr) {
        out += QByteArray(" action=\"") + action + "\"";
    }
    return out + extra + (QByteArray(extra).contains("<a:snd") ? QByteArray() : QByteArray("/>"));
}

// Slide 2's action buttons and linked shapes (ids 10–24), left to right.
inline void addActions(Ooxml::PackageWriter &w)
{
    const QString slide = QStringLiteral("ppt/slides/slide2.xml");
    w.addRelationshipWithId(slide, QStringLiteral("rId20"), kRel + QStringLiteral("slide"), QStringLiteral("ppt/slides/slide4.xml"));
    w.addRelationshipWithId(slide, QStringLiteral("rId21"), kRel + QStringLiteral("hyperlink"), QStringLiteral("https://example.org/m91"), true);
    w.addRelationshipWithId(slide, QStringLiteral("rId22"), kRel + QStringLiteral("hyperlink"), QStringLiteral("notes/brief.docx"), true);
    w.addRelationshipWithId(slide, QStringLiteral("rId23"), kRel + QStringLiteral("hyperlink"), QStringLiteral("file:///C:/Windows/notepad.exe"), true);
    w.addPart(QStringLiteral("ppt/media/audio1.wav"), tinyWav(120), QStringLiteral("audio/wav"));
    w.addRelationshipWithId(slide, QStringLiteral("rId24"), kRel + QStringLiteral("audio"), QStringLiteral("ppt/media/audio1.wav"));
    w.addRelationshipWithId(slide, QStringLiteral("rId25"), kRel + QStringLiteral("slide"), QStringLiteral("ppt/slides/slide3.xml"));
    QByteArray shapes;
    shapes += shape(10, "Action Button: Forward or Next 9", "actionButtonForwardNext", 254000,
                    link("", "ppaction://hlinkshowjump?jump=nextslide", " highlightClick=\"1\"")
                        + link("", "ppaction://hlinkshowjump?jump=lastslide", " tooltip=\"To the end\"", "hlinkHover"));
    shapes += shape(11, "Action Button: Back or Previous 10", "actionButtonBackPrevious", 889000,
                    link("", "ppaction://hlinkshowjump?jump=previousslide"));
    shapes += shape(12, "Action Button: Home 11", "actionButtonHome", 1524000, link("", "ppaction://hlinkshowjump?jump=firstslide"));
    shapes += shape(13, "Action Button: End 12", "actionButtonEnd", 2159000, link("", "ppaction://hlinkshowjump?jump=lastslide"));
    shapes += shape(14, "Action Button: Return 13", "actionButtonReturn", 2794000,
                    link("", "ppaction://hlinkshowjump?jump=lastslideviewed"));
    shapes += shape(15, "End 14", "rect", 3429000, link("", "ppaction://hlinkshowjump?jump=endshow"));
    shapes += shape(16, "To chart 15", "rect", 4064000, link("rId20", "ppaction://hlinksldjump"));
    shapes += shape(17, "Short show 16", "rect", 4699000, link("", "ppaction://customshow?id=1&amp;return=true"));
    shapes += shape(18, "Web 17", "rect", 5334000, link("rId21", nullptr, " tooltip=\"Example\""));
    shapes += shape(19, "Brief 18", "rect", 5969000, link("rId22", "ppaction://hlinkfile"));
    shapes += shape(20, "Notepad 19", "rect", 6604000, link("rId23", "ppaction://program"));
    shapes += shape(21, "Macro 20", "rect", 7239000, link("", "ppaction://macro?name=Refresh"));
    shapes += shape(22, "Applause 21", "rect", 7874000,
                    link("", "ppaction://noaction", " highlightClick=\"1\" endSnd=\"1\"><a:snd r:embed=\"rId24\" name=\"applause.wav\"/></a:hlinkClick>"));
    shapes += shape(23, "Hover 22", "rect", 8509000, link("", "ppaction://hlinkshowjump?jump=nextslide", QByteArray(), "hlinkHover"));
    shapes += R"(<p:sp><p:nvSpPr><p:cNvPr id="24" name="Links 23"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="254000" y="3810000"/><a:ext cx="6096000" cy="508000"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr wrap="square" lIns="0" tIns="0" rIns="0" bIns="0"/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US" sz="2400"><a:hlinkClick r:id="rId25" action="ppaction://hlinksldjump"/></a:rPr><a:t>Pictures</a:t></a:r><a:r><a:rPr lang="en-US" sz="2400"/><a:t> and </a:t></a:r><a:r><a:rPr lang="en-US" sz="2400"><a:hlinkClick r:id="rId21"/></a:rPr><a:t>the web</a:t></a:r></a:p></p:txBody></p:sp>)";
    QByteArray xml = w.partData(slide);
    xml.replace("</p:spTree>", shapes + "</p:spTree>");
    // The transition: Prism in PowerPoint 2010+, Fade for older readers;
    // after 5 s, not on a click (both branches say so).
    xml.replace(R"(<p:transition spd="med"><p:fade/></p:transition>)",
                R"(<mc:AlternateContent xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"><mc:Choice xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" Requires="p14"><p:transition spd="slow" p14:dur="2000" advClick="0" advTm="5000"><p14:prism/></p:transition></mc:Choice><mc:Fallback><p:transition spd="slow" advClick="0" advTm="5000"><p:fade/></p:transition></mc:Fallback></mc:AlternateContent>)");
    w.addPart(slide, xml, kSlideType);
}

// One effect of a main sequence as PowerPoint spells it: the visibility
// switch (entrances) or the hide at the end (exits) and `behaviours`.
inline QByteArray effect(int &id, int preset, const char *cls, int subtype, int spid, const QByteArray &behaviours,
                         const char *nodeType = "clickEffect")
{
    const bool exit = QByteArray(cls) == "exit";
    QByteArray out = QStringLiteral(R"(<p:par><p:cTn id="%1" presetID="%2" presetClass="%3" presetSubtype="%4" fill="hold" grpId="%5" nodeType="%6"><p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>)")
                         .arg(++id)
                         .arg(preset)
                         .arg(QLatin1String(cls))
                         .arg(subtype)
                         .arg(exit ? 1 : 0)
                         .arg(QLatin1String(nodeType))
                         .toUtf8();
    const QByteArray target = "<p:tgtEl><p:spTgt spid=\"" + QByteArray::number(spid) + "\"/></p:tgtEl>";
    if (!exit) {
        out += "<p:set><p:cBhvr><p:cTn id=\"" + QByteArray::number(++id)
            + "\" dur=\"1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst></p:cTn>" + target
            + "<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr><p:to><p:strVal val=\"visible\"/></p:to></p:set>";
    }
    QByteArray body = behaviours;
    while (body.contains("ID")) {
        body.replace(body.indexOf("ID"), 2, QByteArray::number(++id));
    }
    out += body.replace("TARGET", target);
    if (exit) {
        out += "<p:set><p:cBhvr><p:cTn id=\"" + QByteArray::number(++id)
            + "\" dur=\"1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"499\"/></p:stCondLst></p:cTn>" + target
            + "<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr><p:to><p:strVal val=\"hidden\"/></p:to></p:set>";
    }
    return out + "</p:childTnLst></p:cTn></p:par>";
}

inline QByteArray anim(const char *attribute, const char *from, const char *to)
{
    return QByteArray(R"(<p:anim calcmode="lin" valueType="num"><p:cBhvr additive="base"><p:cTn id="ID" dur="500" fill="hold"/>TARGET<p:attrNameLst><p:attrName>)")
        + attribute + R"(</p:attrName></p:attrNameLst></p:cBhvr><p:tavLst><p:tav tm="0"><p:val><p:strVal val=")" + from
        + R"("/></p:val></p:tav><p:tav tm="100000"><p:val><p:strVal val=")" + to + R"("/></p:val></p:tav></p:tavLst></p:anim>)";
}

inline QByteArray filter(const char *transition, const char *name)
{
    return QByteArray(R"(<p:animEffect transition=")") + transition + R"(" filter=")" + name
        + R"("><p:cBhvr><p:cTn id="ID" dur="500"/>TARGET</p:cBhvr></p:animEffect>)";
}

// Slide 4: Advance Slide after 3 s, three rectangles and four clicks —
//   1: the chart (id 2) Fly In from the top left, with rectangle 32 Split horizontal in
//   2: rectangle 30 Peek In from the right
//   3: rectangle 31 Float Down
//   4: rectangle 30 Wipe out from the left (exit)
inline void addEffects(Ooxml::PackageWriter &w)
{
    const QString slide = QStringLiteral("ppt/slides/slide4.xml");
    QByteArray xml = w.partData(slide);
    QByteArray shapes;
    for (const auto &[id, x] : {std::pair{30, 7620000}, std::pair{31, 9144000}, std::pair{32, 10414000}}) {
        shapes += QStringLiteral(R"(<p:sp><p:nvSpPr><p:cNvPr id="%1" name="Rectangle %2"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="%3" y="1270000"/><a:ext cx="1016000" cy="1016000"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val="2E75B6"/></a:solidFill></p:spPr></p:sp>)")
                      .arg(id)
                      .arg(id - 1)
                      .arg(x)
                      .toUtf8();
    }
    xml.replace("</p:spTree>", shapes + "</p:spTree>");
    int id = 2;
    const auto click = [&id](const QByteArray &effects) {
        const int outer = ++id;
        const int inner = ++id;
        QByteArray out = "<p:par><p:cTn id=\"" + QByteArray::number(outer)
            + "\" fill=\"hold\"><p:stCondLst><p:cond delay=\"indefinite\"/></p:stCondLst><p:childTnLst><p:par><p:cTn id=\""
            + QByteArray::number(inner) + "\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>";
        return out + effects + "</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par>";
    };
    QByteArray clicks;
    {
        QByteArray effects = effect(id, 2, "entr", 9, 2, anim("ppt_x", "0-#ppt_w/2", "#ppt_x") + anim("ppt_y", "0-#ppt_h/2", "#ppt_y"));
        effects += effect(id, 16, "entr", 21, 32, filter("in", "barn(inHorizontal)"), "withEffect");
        clicks += click(effects);
    }
    clicks += click(effect(id, 12, "entr", 2, 30, anim("ppt_x", "#ppt_x+#ppt_w*1.125000", "#ppt_x") + filter("in", "wipe(left)")));
    clicks += click(effect(id, 47, "entr", 0, 31, filter("in", "fade") + anim("ppt_x", "#ppt_x", "#ppt_x") + anim("ppt_y", "#ppt_y-.1", "#ppt_y")));
    clicks += click(effect(id, 22, "exit", 8, 30, filter("out", "wipe(right)")));
    const QByteArray timing = R"(<p:timing><p:tnLst><p:par><p:cTn id="1" dur="indefinite" restart="never" nodeType="tmRoot"><p:childTnLst><p:seq concurrent="1" nextAc="seek"><p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst>)"
        + clicks
        + R"(</p:childTnLst></p:cTn><p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst><p:nextCondLst><p:cond evt="onNext" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq></p:childTnLst></p:cTn></p:par></p:tnLst><p:bldLst><p:bldP spid="30" grpId="0" animBg="1"/><p:bldP spid="31" grpId="0" animBg="1"/><p:bldP spid="32" grpId="0" animBg="1"/><p:bldP spid="30" grpId="1" animBg="1"/></p:bldLst></p:timing>)";
    xml.replace("</p:sld>", R"(<p:transition advTm="3000"/>)" + timing + "</p:sld>");
    w.addPart(slide, xml, kSlideType);
}

// Slide 3's video (addVideo) trimmed 1 s at the start and 0.5 s at the
// end, fading in 0.25 s and out 0.75 s, with two bookmarks.
inline void addMediaEdits(Ooxml::PackageWriter &w)
{
    addVideo(w);
    const QString slide = QStringLiteral("ppt/slides/slide3.xml");
    QByteArray xml = w.partData(slide);
    xml.replace(R"(r:embed="rId10"/>)",
                R"(r:embed="rId10"><p14:trim st="1000" end="500.25"/><p14:fade in="250" out="750"/><p14:bmkLst><p14:bmk name="Chorus" time="2100"/><p14:bmk name="Intro" time="200"/></p14:bmkLst></p14:media>)");
    w.addPart(slide, xml, kSlideType);
}

// Two custom shows ("Short": slides 1 and 3; "Data": slides 4 and 2) and
// presProps.xml's Set Up Slide Show.
inline void addShowSettings(Ooxml::PackageWriter &w)
{
    QByteArray xml = w.partData(QStringLiteral("ppt/presentation.xml"));
    xml.replace("<p:defaultTextStyle>",
                R"(<p:custShowLst><p:custShow name="Short" id="1"><p:sldLst><p:sld r:id="rId2"/><p:sld r:id="rId4"/></p:sldLst></p:custShow><p:custShow name="Data" id="4"><p:sldLst><p:sld r:id="rId5"/><p:sld r:id="rId3"/></p:sldLst></p:custShow></p:custShowLst><p:defaultTextStyle>)");
    w.addPart(QStringLiteral("ppt/presentation.xml"), xml,
              QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"));
    w.addPart(QStringLiteral("ppt/presProps.xml"),
              PptxFixture::xml(R"(<p:presentationPr NS><p:showPr loop="1" showNarration="1" useTimings="0"><p:kiosk restart="60000"/><p:sldRg st="2" end="4"/><p:penClr><a:prstClr val="blue"/></p:penClr><p:extLst><p:ext uri="{EC167BDD-8182-4AB7-AECC-EB403E3ABB37}"><p14:laserClr xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main"><a:srgbClr val="00B050"/></p14:laserClr></p:ext><p:ext uri="{2FDB2607-1784-4EEB-B798-7EB5836EED8A}"><p14:showMediaCtrls xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" val="1"/></p:ext></p:extLst></p:showPr><p:extLst><p:ext uri="{E76CE94A-603C-4142-B9EB-6D1370010A27}"><p14:discardImageEditData xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" val="0"/></p:ext></p:extLst></p:presentationPr>)"),
              QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presProps+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/presentation.xml"), QStringLiteral("rId30"), kRel + QStringLiteral("presProps"),
                            QStringLiteral("ppt/presProps.xml"));
}

// Everything above in one deck.
inline QByteArray deck()
{
    return edited([](Ooxml::PackageWriter &w) {
        addActions(w);
        addEffects(w);
        addMediaEdits(w);
        addShowSettings(w);
    });
}

} // namespace PptxFixture::M91
