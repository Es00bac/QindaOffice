// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M75 (D-084) additions to the hand-built PowerPoint package
// (pptx_fixture_slides.h): the base fixture copied into a writer to be
// extended — sections, modern and legacy comments, a video and a sound as
// PowerPoint embeds them — and small media files whose containers say how
// long they play. Everything a test asserts on is spelled out here.

#include "pptx_fixture_slides.h"

#include "ooxml_xml.h"

#include <QtEndian>

#include <functional>

namespace PptxFixture {

// The base fixture's parts and relationships, in a writer to add to.
inline void copyFixture(QindaOffice::Ooxml::PackageWriter &w, const QByteArray &zip = build())
{
    using namespace QindaOffice::Ooxml;
    const std::shared_ptr<const Package> package = Package::fromData(zip);
    QStringList sources{QString()};
    for (const QString &part : package->partNames()) {
        if (part == u"[Content_Types].xml" || part.contains(QLatin1String("_rels/"))) {
            continue;
        }
        w.addPart(part, package->data(part), package->contentType(part));
        sources.append(part);
    }
    for (const QString &source : std::as_const(sources)) {
        for (const Relationship &rel : package->relationships(source)) {
            w.addRelationshipWithId(source, rel.id, rel.type, rel.external ? rel.target : package->targetPart(source, rel),
                                    rel.external);
        }
    }
}

inline QByteArray edited(const std::function<void(QindaOffice::Ooxml::PackageWriter &)> &edit)
{
    QindaOffice::Ooxml::PackageWriter w;
    copyFixture(w);
    edit(w);
    return w.toZip();
}

// presentation.xml with the extension list `ext` (sections and the like).
inline void presentationExtension(QindaOffice::Ooxml::PackageWriter &w, const QByteArray &ext)
{
    QByteArray xml = w.partData(QStringLiteral("ppt/presentation.xml"));
    xml.replace("</p:presentation>", "<p:extLst>" + ext + "</p:extLst></p:presentation>");
    w.addPart(QStringLiteral("ppt/presentation.xml"), xml,
              QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"));
}

// Two sections: "Intro" (slides 1-2) and "Data" (slides 3-5).
inline const char *const kSections =
    R"(<p:ext uri="{521415D9-36F7-43E2-AB2F-B90AF26B5E84}"><p14:sectionLst xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main"><p14:section name="Intro" id="{8A6B1E41-6C2F-4A0B-9C1D-2E3F40516273}"><p14:sldIdLst><p14:sldId id="256"/><p14:sldId id="257"/></p14:sldIdLst></p14:section><p14:section name="Data" id="{1F2E3D4C-5B6A-4798-8A9B-0C1D2E3F4A5B}"><p14:sldIdLst><p14:sldId id="258"/><p14:sldId id="259"/><p14:sldId id="260"/></p14:sldIdLst></p14:section></p14:sectionLst></p:ext>)";

// ---- comments ----
inline const char *const kModernAuthors =
    R"(<p188:authorLst xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p188="http://schemas.microsoft.com/office/powerpoint/2018/8/main"><p188:author id="{AAAA0000-1111-2222-3333-444455556666}" name="Ada Lovelace" initials="AL" userId="Ada Lovelace" providerId="None"/><p188:author id="{BBBB0000-1111-2222-3333-444455556666}" name="Alan Turing" initials="AT" userId="Alan Turing" providerId="None"/></p188:authorLst>)";

// Slide 1: a thread on the text box (id 4) with a reply, and a resolved
// comment on the slide.
inline const char *const kModernComments =
    R"(<p188:cmLst xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p188="http://schemas.microsoft.com/office/powerpoint/2018/8/main">)"
    R"(<p188:cm id="{C0000000-0000-4000-8000-000000000001}" authorId="{AAAA0000-1111-2222-3333-444455556666}" created="2026-09-01T10:00:00.000"><ac:deMkLst xmlns:ac="http://schemas.microsoft.com/office/drawing/2013/main/command"><pc:docMk xmlns:pc="http://schemas.microsoft.com/office/powerpoint/2013/main/command"/><pc:sldMk xmlns:pc="http://schemas.microsoft.com/office/powerpoint/2013/main/command" cId="1234567" sldId="256"/><ac:spMk id="4" creationId="{D1D1D1D1-0000-4000-8000-000000000004}"/></ac:deMkLst>)"
    R"(<p188:replyLst><p188:reply id="{C0000000-0000-4000-8000-000000000002}" authorId="{BBBB0000-1111-2222-3333-444455556666}" created="2026-09-01T11:30:00.000"><p188:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Agreed, darker.</a:t></a:r></a:p></p188:txBody></p188:reply></p188:replyLst>)"
    R"(<p188:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Is this accent dark enough?</a:t></a:r></a:p></p188:txBody></p188:cm>)"
    R"(<p188:cm id="{C0000000-0000-4000-8000-000000000003}" authorId="{BBBB0000-1111-2222-3333-444455556666}" created="2026-09-02T09:15:00.000" status="resolved"><pc:sldMkLst xmlns:pc="http://schemas.microsoft.com/office/powerpoint/2013/main/command"><pc:docMk/><pc:sldMk cId="1234567" sldId="256"/></pc:sldMkLst><p188:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Title approved</a:t></a:r></a:p></p188:txBody></p188:cm>)"
    R"(</p188:cmLst>)";

inline void addModernComments(QindaOffice::Ooxml::PackageWriter &w)
{
    const QString rel18 = QStringLiteral("http://schemas.microsoft.com/office/2018/10/relationships/");
    w.addPart(QStringLiteral("ppt/authors.xml"), xml(kModernAuthors), QStringLiteral("application/vnd.ms-powerpoint.authors+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/presentation.xml"), QStringLiteral("rId20"), rel18 + QStringLiteral("authors"),
                            QStringLiteral("ppt/authors.xml"));
    const QString part = QStringLiteral("ppt/comments/modernComment_100_12D687.xml");
    w.addPart(part, xml(kModernComments), QStringLiteral("application/vnd.ms-powerpoint.comments+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/slides/slide1.xml"), QStringLiteral("rId9"), rel18 + QStringLiteral("comments"), part);
    QByteArray slide = w.partData(QStringLiteral("ppt/slides/slide1.xml"));
    slide.replace("</p:sld>", R"(<p:extLst><p:ext uri="{BB962C8B-B14F-4D97-AF65-F5344CB8AC3E}"><p14:creationId xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" val="1234567"/></p:ext><p:ext uri="{6950BFC3-D8DA-4A85-94F7-54DA5524770B}"><p188:commentRel xmlns:p188="http://schemas.microsoft.com/office/powerpoint/2018/8/main" r:id="rId9"/></p:ext></p:extLst></p:sld>)");
    w.addPart(QStringLiteral("ppt/slides/slide1.xml"), slide,
              QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slide+xml"));
}

// Slide 2: a legacy comment at (100 pt, 50 pt) and a threaded reply.
inline void addLegacyComments(QindaOffice::Ooxml::PackageWriter &w)
{
    const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
    const QString pml = QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.");
    w.addPart(QStringLiteral("ppt/commentAuthors.xml"),
              xml(R"(<p:cmAuthorLst NS><p:cmAuthor id="0" name="Grace Hopper" initials="GH" lastIdx="2" clrIdx="0"/><p:cmAuthor id="1" name="Edsger Dijkstra" initials="ED" lastIdx="1" clrIdx="1"/></p:cmAuthorLst>)"),
              pml + QStringLiteral("commentAuthors+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/presentation.xml"), QStringLiteral("rId21"), rel + QStringLiteral("commentAuthors"),
                            QStringLiteral("ppt/commentAuthors.xml"));
    w.addPart(QStringLiteral("ppt/comments/comment1.xml"),
              xml(R"(<p:cmLst NS><p:cm authorId="0" dt="2026-08-30T08:00:00.000" idx="2"><p:pos x="800" y="400"/><p:text>Check the merged header</p:text></p:cm>)"
                  R"(<p:cm authorId="1" dt="2026-08-30T09:00:00.000" idx="1"><p:pos x="800" y="400"/><p:text>Done</p:text><p:extLst><p:ext uri="{C676402C-5697-4E1C-873F-D02D1690AC5C}"><p15:threadingInfo xmlns:p15="http://schemas.microsoft.com/office/powerpoint/2012/main" timeZoneBias="0"><p15:parentCm authorId="0" idx="2"/></p15:threadingInfo></p:ext></p:extLst></p:cm></p:cmLst>)"),
              pml + QStringLiteral("comments+xml"));
    w.addRelationshipWithId(QStringLiteral("ppt/slides/slide2.xml"), QStringLiteral("rId9"), rel + QStringLiteral("comments"),
                            QStringLiteral("ppt/comments/comment1.xml"));
}

// ---- media ----
// An ISO BMFF file (MP4) that says it plays `ms` milliseconds: ftyp, then
// moov with an mvhd (timescale 1000) and a video track's handler.
inline QByteArray tinyMp4(quint32 ms, bool video = true)
{
    const auto box = [](const char *type, const QByteArray &payload) {
        QByteArray out(4, '\0');
        qToBigEndian<quint32>(quint32(8 + payload.size()), out.data());
        return out + QByteArray(type, 4) + payload;
    };
    QByteArray mvhd(100, '\0');
    qToBigEndian<quint32>(1000, mvhd.data() + 12); // timescale
    qToBigEndian<quint32>(ms, mvhd.data() + 16);   // duration
    QByteArray hdlr(24, '\0');
    hdlr.replace(8, 4, video ? "vide" : "soun");
    const QByteArray trak = box("trak", box("mdia", box("hdlr", hdlr)));
    return box("ftyp", QByteArray(video ? "isom" : "M4A ") + QByteArray(4, '\0') + "isommp42") + box("moov", box("mvhd", mvhd) + trak)
        + box("mdat", QByteArray(64, '\x55'));
}

// A RIFF WAVE file of `ms` milliseconds of 8 kHz mono 8-bit silence.
inline QByteArray tinyWav(quint32 ms)
{
    const quint32 bytes = 8 * ms;
    QByteArray out("RIFF....WAVEfmt ");
    QByteArray fmt(20, '\0');
    qToLittleEndian<quint32>(16, fmt.data());
    qToLittleEndian<quint16>(1, fmt.data() + 4);    // PCM
    qToLittleEndian<quint16>(1, fmt.data() + 6);    // mono
    qToLittleEndian<quint32>(8000, fmt.data() + 8); // rate
    qToLittleEndian<quint32>(8000, fmt.data() + 12); // byte rate
    qToLittleEndian<quint16>(1, fmt.data() + 16);
    qToLittleEndian<quint16>(8, fmt.data() + 18);
    out += fmt.mid(0, 20) + "data";
    QByteArray size(4, '\0');
    qToLittleEndian<quint32>(bytes, size.data());
    out += size + QByteArray(int(bytes), '\x80');
    qToLittleEndian<quint32>(quint32(out.size() - 8), out.data() + 4);
    return out;
}

// Slide 3 gets a video as PowerPoint 2016 inserts one (poster, the media
// and video relationships, p14:media) that starts automatically, loops and
// hides while not playing.
inline void addVideo(QindaOffice::Ooxml::PackageWriter &w)
{
    const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
    w.addPart(QStringLiteral("ppt/media/media1.mp4"), tinyMp4(4321), QStringLiteral("video/mp4"));
    w.addPart(QStringLiteral("ppt/media/image9.png"), pngBytes(QColor(20, 20, 20), QSize(64, 36)));
    const QString slide = QStringLiteral("ppt/slides/slide3.xml");
    w.addRelationshipWithId(slide, QStringLiteral("rId10"), QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/media"),
                            QStringLiteral("ppt/media/media1.mp4"));
    w.addRelationshipWithId(slide, QStringLiteral("rId11"), rel + QStringLiteral("video"), QStringLiteral("ppt/media/media1.mp4"));
    w.addRelationshipWithId(slide, QStringLiteral("rId12"), rel + QStringLiteral("image"), QStringLiteral("ppt/media/image9.png"));
    QByteArray xml = w.partData(slide);
    xml.replace("</p:spTree>",
                R"(<p:pic><p:nvPicPr><p:cNvPr id="7" name="clip"><a:hlinkClick r:id="" action="ppaction://media"/></p:cNvPr><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr><a:videoFile r:link="rId11"/><p:extLst><p:ext uri="{DAA4B4D4-6D71-4841-9C94-3DA2E9CE8D0B}"><p14:media xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" r:embed="rId10"/></p:ext></p:extLst></p:nvPr></p:nvPicPr><p:blipFill><a:blip r:embed="rId12"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x="6350000" y="3810000"/><a:ext cx="3200400" cy="1800225"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic></p:spTree>)");
    xml.replace("</p:sld>",
                R"x(<p:timing><p:tnLst><p:par><p:cTn id="1" dur="indefinite" restart="never" nodeType="tmRoot"><p:childTnLst><p:seq concurrent="1" nextAc="seek"><p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst><p:par><p:cTn id="3" fill="hold"><p:stCondLst><p:cond delay="indefinite"/><p:cond evt="onBegin" delay="0"><p:tn val="2"/></p:cond></p:stCondLst><p:childTnLst><p:par><p:cTn id="4" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst><p:par><p:cTn id="5" presetID="1" presetClass="mediacall" presetSubtype="0" fill="hold" nodeType="afterEffect"><p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst><p:cmd type="call" cmd="playFrom(0.0)"><p:cBhvr><p:cTn id="6" dur="4321" fill="hold"/><p:tgtEl><p:spTgt spid="7"/></p:tgtEl></p:cBhvr></p:cmd></p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn><p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst><p:nextCondLst><p:cond evt="onNext" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq><p:video><p:cMediaNode vol="80000" showWhenStopped="0"><p:cTn id="7" repeatCount="indefinite" fill="hold" display="0"><p:stCondLst><p:cond delay="indefinite"/></p:stCondLst></p:cTn><p:tgtEl><p:spTgt spid="7"/></p:tgtEl></p:cMediaNode></p:video></p:childTnLst></p:cTn></p:par></p:tnLst></p:timing></p:sld>)x");
    w.addPart(slide, xml, QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slide+xml"));
}

} // namespace PptxFixture
