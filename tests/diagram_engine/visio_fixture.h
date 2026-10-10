// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Hand-built Visio packages for the M65 Visio gates (D-073). The owner has
// no Visio files, so every fixture is written here, part by part, in the
// shape Visio 2013+ writes them (default namespace, xml:space="preserve",
// IX-numbered rows, V values in inches) — the structure was checked against
// Microsoft's [MS-VSDX] schema map and Visio-written samples, nothing of
// which is in the repository.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace VisioFixture {

inline const char *kHead =
    "<?xml version='1.0' encoding='utf-8' ?>";
inline const char *kNs =
    "xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
    "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships' xml:space='preserve'";

inline QString str(const char *text)
{
    return QString::fromUtf8(text);
}

// Style sheets: 0 No Style (Calibri 12 pt black, 0.75 pt black line, white
// fill), 3 Normal (red line), 7 Thin (inherits Normal; a blue fill).
inline QString defaultStyles()
{
    return str("<StyleSheets>"
               "<StyleSheet ID='0' NameU='No Style' Name='No Style'>"
               "<Cell N='LineWeight' V='0.01041666666666667'/><Cell N='LineColor' V='0'/><Cell N='LinePattern' V='1'/>"
               "<Cell N='FillForegnd' V='1'/><Cell N='FillBkgnd' V='0'/><Cell N='FillPattern' V='1'/>"
               "<Cell N='ShdwPattern' V='0'/><Cell N='VerticalAlign' V='1'/><Cell N='BeginArrow' V='0'/>"
               "<Cell N='EndArrow' V='0'/><Cell N='LeftMargin' V='0'/><Cell N='RightMargin' V='0'/>"
               "<Cell N='TopMargin' V='0'/><Cell N='BottomMargin' V='0'/>"
               "<Section N='Character'><Row IX='0'><Cell N='Font' V='Calibri'/><Cell N='Color' V='0'/>"
               "<Cell N='Style' V='0'/><Cell N='Size' V='0.1666666666666667'/></Row></Section>"
               "<Section N='Paragraph'><Row IX='0'><Cell N='HorzAlign' V='1'/></Row></Section>"
               "</StyleSheet>"
               "<StyleSheet ID='3' NameU='Normal' Name='Normal' LineStyle='0' FillStyle='0' TextStyle='0'>"
               "<Cell N='LineColor' V='#ff0000'/></StyleSheet>"
               "<StyleSheet ID='7' NameU='Thin' Name='Thin' LineStyle='3' FillStyle='3' TextStyle='3'>"
               "<Cell N='FillForegnd' V='#0000ff'/></StyleSheet>"
               "</StyleSheets>");
}

inline QString documentXml(const QString &extra = QString())
{
    return QString::fromLatin1(kHead) + QStringLiteral("<VisioDocument %1>").arg(QString::fromLatin1(kNs))
        + str("<DocumentSettings TopPage='0' DefaultTextStyle='3' DefaultLineStyle='3' DefaultFillStyle='3' "
              "DefaultGuideStyle='4'><GlueSettings>9</GlueSettings></DocumentSettings>"
              "<Colors><ColorEntry IX='24' RGB='#123456'/></Colors>")
        + defaultStyles() + extra + str("</VisioDocument>");
}

struct PageSpec {
    QString name;
    double width = 11;   // inches
    double height = 8.5;
    QString contents;    // the inside of <PageContents>: <Shapes>…</Shapes><Connects>…</Connects>
    QString sheetExtra;  // more PageSheet cells and sections
    bool background = false;
    int backPage = -1;
    QList<QPair<QString, QByteArray>> media; // (relationship id, bytes as image1.png …)
};

struct MasterSpec {
    int id = 1;
    QString name;
    QString contents; // the inside of <MasterContents>
};

struct ExtraPart {
    QString name;
    QByteArray bytes;
    QString contentType;
    QString relType; // from the document part
    // M87: a part related from another extra part (recordset1.xml from
    // recordsets.xml) names that part here, and the relationship id.
    QString parent;
    QString relId;
};

// A whole package: [Content_Types], rels, docProps, document, pages,
// masters, an optional theme and extra document-level parts.
inline QByteArray build(const QList<PageSpec> &pages, const QList<MasterSpec> &masters = {},
                        const QString &documentExtra = {}, const QByteArray &theme = {},
                        const QList<ExtraPart> &extra = {})
{
    using namespace QindaOffice::Ooxml;
    PackageWriter w;
    const QString doc = str("visio/document.xml");
    w.addPart(doc, documentXml(documentExtra).toUtf8(), str("application/vnd.ms-visio.drawing.main+xml"));
    w.addRelationship(QString(), str("http://schemas.microsoft.com/visio/2010/relationships/document"), doc);
    QString pagesXml = QString::fromLatin1(kHead) + QStringLiteral("<Pages %1>").arg(QString::fromLatin1(kNs));
    for (int i = 0; i < pages.size(); ++i) {
        const PageSpec &p = pages.at(i);
        const QString part = QStringLiteral("visio/pages/page%1.xml").arg(i + 1);
        w.addPart(part,
                  (QString::fromLatin1(kHead) + QStringLiteral("<PageContents %1>").arg(QString::fromLatin1(kNs))
                   + p.contents + str("</PageContents>"))
                      .toUtf8(),
                  str("application/vnd.ms-visio.page+xml"));
        for (const auto &[rel, bytes] : p.media) {
            const QString media = QStringLiteral("visio/media/%1").arg(rel);
            w.addPart(media, bytes);
            w.addRelationshipWithId(part, rel, QString::fromUtf16(RelType::Image), media);
        }
        const QString id = w.addRelationship(str("visio/pages/pages.xml"),
                                             str("http://schemas.microsoft.com/visio/2010/relationships/page"), part);
        for (const MasterSpec &m : masters) {
            w.addRelationship(part, str("http://schemas.microsoft.com/visio/2010/relationships/master"),
                              QStringLiteral("visio/masters/master%1.xml").arg(m.id));
        }
        pagesXml += QStringLiteral("<Page ID='%1' NameU='%2' Name='%2'%3%4><PageSheet LineStyle='0' FillStyle='0' "
                                   "TextStyle='0'><Cell N='PageWidth' V='%5'/><Cell N='PageHeight' V='%6'/>%7"
                                   "</PageSheet><Rel r:id='%8'/></Page>")
                        .arg(i)
                        .arg(p.name)
                        .arg(p.background ? str(" Background='1'") : QString())
                        .arg(p.backPage >= 0 ? QStringLiteral(" BackPage='%1'").arg(p.backPage) : QString())
                        .arg(p.width)
                        .arg(p.height)
                        .arg(p.sheetExtra, id);
    }
    pagesXml += str("</Pages>");
    w.addPart(str("visio/pages/pages.xml"), pagesXml.toUtf8(), str("application/vnd.ms-visio.pages+xml"));
    w.addRelationship(doc, str("http://schemas.microsoft.com/visio/2010/relationships/pages"), str("visio/pages/pages.xml"));
    if (!masters.isEmpty()) {
        QString mastersXml = QString::fromLatin1(kHead) + QStringLiteral("<Masters %1>").arg(QString::fromLatin1(kNs));
        for (const MasterSpec &m : masters) {
            const QString part = QStringLiteral("visio/masters/master%1.xml").arg(m.id);
            w.addPart(part,
                      (QString::fromLatin1(kHead) + QStringLiteral("<MasterContents %1>").arg(QString::fromLatin1(kNs))
                       + m.contents + str("</MasterContents>"))
                          .toUtf8(),
                      str("application/vnd.ms-visio.master+xml"));
            const QString id = w.addRelationship(str("visio/masters/masters.xml"),
                                                 str("http://schemas.microsoft.com/visio/2010/relationships/master"), part);
            mastersXml += QStringLiteral("<Master ID='%1' NameU='%2' Name='%2'><Rel r:id='%3'/></Master>")
                              .arg(m.id)
                              .arg(m.name, id);
        }
        mastersXml += str("</Masters>");
        w.addPart(str("visio/masters/masters.xml"), mastersXml.toUtf8(), str("application/vnd.ms-visio.masters+xml"));
        w.addRelationship(doc, str("http://schemas.microsoft.com/visio/2010/relationships/masters"),
                          str("visio/masters/masters.xml"));
    }
    if (!theme.isEmpty()) {
        w.addPart(str("visio/theme/theme1.xml"), theme, QString::fromUtf16(ContentType::Theme));
        w.addRelationship(doc, QString::fromUtf16(RelType::Theme), str("visio/theme/theme1.xml"));
    }
    for (const ExtraPart &part : extra) {
        w.addPart(part.name, part.bytes, part.contentType);
        if (part.parent.isEmpty()) {
            w.addRelationship(doc, part.relType, part.name);
        } else {
            w.addRelationshipWithId(part.parent, part.relId, part.relType, part.name);
        }
    }
    return w.toZip();
}

// A rectangle W × H as one Geometry section (absolute rows).
inline QString rect(double w, double h)
{
    return QStringLiteral("<Section N='Geometry' IX='0'><Row T='MoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>"
                          "<Row T='LineTo' IX='2'><Cell N='X' V='%1'/><Cell N='Y' V='0'/></Row>"
                          "<Row T='LineTo' IX='3'><Cell N='X' V='%1'/><Cell N='Y' V='%2'/></Row>"
                          "<Row T='LineTo' IX='4'><Cell N='X' V='0'/><Cell N='Y' V='%2'/></Row>"
                          "<Row T='LineTo' IX='5'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row></Section>")
        .arg(w)
        .arg(h);
}

// PinX … LocPinY cells (pin at the centre), then `more`.
inline QString xform(double pinX, double pinY, double w, double h, const QString &more = {})
{
    return QStringLiteral("<Cell N='PinX' V='%1'/><Cell N='PinY' V='%2'/><Cell N='Width' V='%3'/><Cell N='Height' V='%4'/>"
                          "<Cell N='LocPinX' V='%5' F='Width*0.5'/><Cell N='LocPinY' V='%6' F='Height*0.5'/>")
               .arg(pinX)
               .arg(pinY)
               .arg(w)
               .arg(h)
               .arg(w / 2)
               .arg(h / 2)
        + more;
}

inline QString ports()
{
    return str("<Section N='Connection'><Row T='Connection' IX='0'><Cell N='X' V='0' F='Width*0'/>"
               "<Cell N='Y' V='0.5' F='Height*0.5'/></Row><Row T='Connection' IX='1'><Cell N='X' V='2' F='Width*1'/>"
               "<Cell N='Y' V='0.5' F='Height*0.5'/></Row></Section>");
}

inline QByteArray smallTheme()
{
    return "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>"
           "<a:theme xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' name='Office'><a:themeElements>"
           "<a:clrScheme name='Office'><a:dk1><a:srgbClr val='000000'/></a:dk1><a:lt1><a:srgbClr val='FFFFFF'/></a:lt1>"
           "<a:dk2><a:srgbClr val='44546A'/></a:dk2><a:lt2><a:srgbClr val='E7E6E6'/></a:lt2>"
           "<a:accent1><a:srgbClr val='4472C4'/></a:accent1><a:accent2><a:srgbClr val='ED7D31'/></a:accent2>"
           "<a:accent3><a:srgbClr val='A5A5A5'/></a:accent3><a:accent4><a:srgbClr val='FFC000'/></a:accent4>"
           "<a:accent5><a:srgbClr val='5B9BD5'/></a:accent5><a:accent6><a:srgbClr val='70AD47'/></a:accent6>"
           "<a:hlink><a:srgbClr val='0563C1'/></a:hlink><a:folHlink><a:srgbClr val='954F72'/></a:folHlink>"
           "</a:clrScheme><a:fontScheme name='Office'><a:majorFont><a:latin typeface='Calibri Light'/></a:majorFont>"
           "<a:minorFont><a:latin typeface='Calibri'/></a:minorFont></a:fontScheme></a:themeElements></a:theme>";
}

// A small office plan with everything the writer must keep: a master
// instance with user cells, a hyperlink and a layer; shape data; a group;
// a guide; a connector glued both ways; a second page; a theme; and a
// document-level part the model knows nothing of (validation rules).
//   page "Plan": 1 Alpha (master Box), 2 Beta, 3 Gamma, 4 Delta,
//   5 group of 6 "G1" and 7 "G2", 8 guide, 9 connector 1 → 2, 10 Epsilon.
inline QByteArray officePackage(const QList<ExtraPart> &more = {})
{
    MasterSpec box;
    box.id = 1;
    box.name = str("Box");
    box.contents = str("<Shapes><Shape ID='5' Type='Shape' LineStyle='3' FillStyle='3' TextStyle='3'>") + xform(1, 0.5, 2, 1)
                 + str("<Cell N='FillForegnd' V='#5b9bd5'/><Section N='Property'><Row N='Cost'><Cell N='Value' V='12'/>"
                       "<Cell N='Label' V='Cost'/></Row></Section>")
                 + ports() + rect(2, 1) + str("<Text>Box</Text></Shape></Shapes>");
    PageSpec plan;
    plan.name = str("Plan");
    plan.sheetExtra = str("<Section N='Layer'><Row IX='0'><Cell N='Name' V='Flow'/><Cell N='Visible' V='1'/></Row></Section>");
    const QString shape = str("<Shape ID='%1' Type='Shape' LineStyle='3' FillStyle='3' TextStyle='3'>%2</Shape>");
    plan.contents =
        str("<Shapes><Shape ID='1' NameU='Box' Name='Box' Type='Shape' Master='1' UniqueID='{0D1B4A7E-0000-4000-8000-000000000001}'>"
            "<Cell N='PinX' V='2'/><Cell N='PinY' V='6'/><Cell N='LayerMember' V='0'/>"
            "<Section N='User'><Row N='Tag'><Cell N='Value' V='a' U='STR'/><Cell N='Prompt' V=''/></Row></Section>"
            "<Section N='Hyperlink'><Row N='Row_1'><Cell N='Address' V='https://example.org/'/></Row></Section>"
            "<Text>Alpha</Text></Shape>")
        + shape.arg(2).arg(xform(5, 6, 2, 1) + ports() + rect(2, 1)
                           + str("<Section N='Property'><Row N='Owner'><Cell N='Value' V='Sam' U='STR'/>"
                                 "<Cell N='Label' V='Owner'/></Row></Section><Text>Beta</Text>"))
        + shape.arg(3).arg(xform(8, 6, 2, 1, str("<Cell N='FillForegnd' V='#ff8800'/>")) + rect(2, 1) + str("<Text>Gamma</Text>"))
        + shape.arg(4).arg(xform(2, 3, 1, 1) + rect(1, 1) + str("<Text>Delta</Text>"))
        + str("<Shape ID='5' Type='Group'>") + xform(6, 3, 2, 1) + str("<Shapes>")
        + shape.arg(6).arg(xform(0.5, 0.5, 1, 1) + rect(1, 1) + str("<Text>G1</Text>"))
        + shape.arg(7).arg(xform(1.5, 0.5, 1, 1) + rect(1, 1) + str("<Text>G2</Text>")) + str("</Shapes></Shape>")
        + str("<Shape ID='8' Type='Guide' LineStyle='0' FillStyle='0' TextStyle='0'><Cell N='PinY' V='2'/>"
              "<Section N='Geometry' IX='0'><Row T='InfiniteLine' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/>"
              "<Cell N='A' V='1'/><Cell N='B' V='0'/></Row></Section></Shape>")
        + shape.arg(9).arg(
            xform(3.5, 6, 1, 0.25,
                  str("<Cell N='BeginX' V='3' F='_WALKGLUE(BegTrigger,EndTrigger,WalkPreference)'/>"
                      "<Cell N='BeginY' V='6' F='_WALKGLUE(BegTrigger,EndTrigger,WalkPreference)'/>"
                      "<Cell N='EndX' V='4' F='PAR(PNT(Sheet.2!Connections.X1,Sheet.2!Connections.Y1))'/>"
                      "<Cell N='EndY' V='6' F='PAR(PNT(Sheet.2!Connections.X1,Sheet.2!Connections.Y1))'/>"
                      "<Cell N='BegTrigger' V='2' F='_XFTRIGGER(Sheet.1!EventXFMod)'/>"
                      "<Cell N='EndTrigger' V='2' F='_XFTRIGGER(Sheet.2!EventXFMod)'/><Cell N='EndArrow' V='4'/>"))
            + str("<Section N='Geometry' IX='0'><Cell N='NoFill' V='1'/><Row T='MoveTo' IX='1'><Cell N='X' V='0'/>"
                  "<Cell N='Y' V='0.125'/></Row><Row T='LineTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='0.125'/></Row>"
                  "</Section>"))
        + shape.arg(10).arg(xform(9, 3, 1, 1) + rect(1, 1) + str("<Text>Epsilon</Text>")) + str("</Shapes>")
        + str("<Connects><Connect FromSheet='9' FromCell='BeginX' FromPart='9' ToSheet='1' ToCell='PinX' ToPart='3'/>"
              "<Connect FromSheet='9' FromCell='EndX' FromPart='12' ToSheet='2' ToCell='Connections.X1' ToPart='100'/>"
              "</Connects>");
    PageSpec notes;
    notes.name = str("Notes");
    notes.contents = str("<Shapes>") + shape.arg(1).arg(xform(2, 2, 2, 1) + rect(2, 1) + str("<Text>Note</Text>"))
                   + str("</Shapes>");
    QList<ExtraPart> extra{{str("visio/validation.xml"),
                            QByteArray("<?xml version='1.0' encoding='utf-8' ?><Validation "
                                       "xmlns='http://schemas.microsoft.com/office/visio/2012/main' xml:space='preserve'>"
                                       "<ValidationProperties/></Validation>"),
                            str("application/vnd.ms-visio.validation+xml"),
                            str("http://schemas.microsoft.com/visio/2010/relationships/validation"), {}, {}}};
    extra.append(more);
    return build({plan, notes}, {box}, {}, smallTheme(), extra);
}

} // namespace VisioFixture
