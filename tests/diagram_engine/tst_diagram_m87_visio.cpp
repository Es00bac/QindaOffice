// SPDX-License-Identifier: GPL-3.0-or-later
// M87 (D-095) in .vsdx: a Visio-shaped package with layers, a hidden layer,
// hyperlinks, a swimlane container with a member, comments, a background
// page, print setup, line jumps and a data recordset linked to a shape
// reads into the model and — untouched — writes back byte for byte; edits
// rewrite only what they touch (a renamed layer, a new comment, refreshed
// data); a drawing made here carries all of it as Visio writes it, reads
// back equal and passes the shared OPC/schema check (QQO_OOXML_SCHEMA_DIR);
// and headless LibreOffice renders it (skipped without soffice/pdftotext).

#include "visio_fixture.h"

#include "data_link.h"
#include "layers.h"
#include "ops.h"
#include "visio_io.h"

#include "ooxml_package.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

using namespace QindaOffice::Diagram;
using namespace QindaOffice::Ooxml;
using VisioFixture::str;

namespace {

const QString kNs = QStringLiteral("http://schemas.microsoft.com/office/visio/2012/main");

QString layerRow(int ix, const char *name, const char *color, int visible)
{
    return QStringLiteral("<Row IX='%1'><Cell N='Name' V='%2'/><Cell N='Color' V='%3'/><Cell N='Status' V='0'/>"
                          "<Cell N='Visible' V='%4'/><Cell N='Print' V='1'/><Cell N='Active' V='0'/><Cell N='Lock' V='0'/>"
                          "<Cell N='Snap' V='1'/><Cell N='Glue' V='1'/><Cell N='NameUniv' V='%2'/><Cell N='ColorTrans' V='0'/></Row>")
        .arg(ix)
        .arg(str(name), str(color))
        .arg(visible);
}

QString dataRow(const char *column, const char *value)
{
    return QStringLiteral("<Row N='_VisDM_%1'><Cell N='Value' V='%2' U='STR'/><Cell N='Prompt' V=''/><Cell N='Label' V='%1'/>"
                          "<Cell N='DataLinked' V='1'/></Row>")
        .arg(str(column), str(value));
}

// The package: page "Plan" (ID 0, BackPage 1) and background page "Back" (ID 1).
QByteArray m87Package()
{
    using namespace VisioFixture;
    PageSpec plan;
    plan.name = str("Plan");
    plan.backPage = 1;
    plan.sheetExtra = str("<Cell N='LineJumpCode' V='1'/><Cell N='LineJumpStyle' V='2'/><Cell N='DrawingResizeType' V='2'/>"
                          "<Cell N='OnPage' V='0'/><Cell N='ScaleX' V='1'/><Cell N='ScaleY' V='1'/><Cell N='PaperKind' V='1'/>"
                          "<Cell N='PrintPageOrientation' V='2'/><Cell N='CenterX' V='0'/><Cell N='CenterY' V='0'/>"
                          "<Section N='Layer'>")
                    + layerRow(0, "Flow", "255", 1) + layerRow(1, "Notes", "#c00000", 0) + str("</Section>");
    const QString shape = str("<Shape ID='%1' Type='Shape' LineStyle='3' FillStyle='3' TextStyle='3'>%2</Shape>");
    plan.contents =
        str("<Shapes>")
        + shape.arg(1).arg(xform(2, 2, 2, 1, str("<Cell N='LayerMember' V='0'/>")) + rect(2, 1)
                           + str("<Section N='Hyperlink'><Row N='Row_1'><Cell N='Description' V='Spec'/>"
                                 "<Cell N='Address' V='https://example.org/spec'/><Cell N='SubAddress' V=''/></Row></Section>"
                                 "<Text>Alpha</Text>"))
        + shape.arg(2).arg(xform(5, 2, 2, 1, str("<Cell N='LayerMember' V='1'/>")) + rect(2, 1) + str("<Text>Hidden note</Text>"))
        + shape.arg(3).arg(xform(4, 6, 6, 2, str("<Cell N='Relationships' V='0' F='SUM(DEPENDSON(1,Sheet.4!SheetRef()))'/>"))
                           + str("<Section N='User'><Row N='msvStructureType'><Cell N='Value' V='Container' U='STR'/></Row>"
                                 "<Row N='msvShapeCategories'><Cell N='Value' V='Swimlane;DoNotContain' U='STR'/></Row></Section>")
                           + rect(6, 2) + str("<Text>Sales</Text>"))
        + shape.arg(4).arg(xform(3, 6, 1, 0.5, str("<Cell N='Relationships' V='0' F='SUM(DEPENDSON(4,Sheet.3!SheetRef()))'/>"))
                           + str("<Section N='Property'>") + dataRow("Name", "Step") + dataRow("Owner", "Sam")
                           + str("</Section>") + rect(1, 0.5) + str("<Text>Step</Text>"))
        + str("</Shapes>");
    PageSpec back;
    back.name = str("Back");
    back.background = true;
    back.contents = str("<Shapes>") + shape.arg(1).arg(xform(5, 8, 4, 0.5) + rect(4, 0.5) + str("<Text>Title block</Text>"))
                  + str("</Shapes>");
    const QByteArray comments =
        "<?xml version='1.0' encoding='utf-8' ?><Comments xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
        "xml:space='preserve' ShowCommentTags='1'><AuthorList><AuthorEntry Name='Dana Brooks' Initials='DB' ID='1'/>"
        "</AuthorList><CommentList><CommentEntry AuthorID='1' PageID='0' ShapeID='1' Date='2026-04-12T13:17:15' "
        "EditDate='2026-04-12T13:17:15' CommentID='7' AutoCommentType='0'>Check the spec</CommentEntry>"
        "<CommentEntry AuthorID='1' PageID='0' Date='2026-04-13T09:00:00' Done='1' CommentID='9'>Page note</CommentEntry>"
        "</CommentList></Comments>";
    const QByteArray recordsets =
        "<?xml version='1.0' encoding='utf-8' ?><DataRecordSets NextID='2' ActiveRecordsetID='1' DataWindowOrder='1;' "
        "xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
        "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships' xml:space='preserve'>"
        "<DataRecordSet ID='1' Name='Steps' TimeRefreshed='2026-04-12T10:00:00' NextRowID='3' Checksum='0'>"
        "<Rel r:id='rId1'/><DataColumns><DataColumn ColumnNameID='Name' Name='_VisDM_Name' Label='Name'/>"
        "<DataColumn ColumnNameID='Owner' Name='_VisDM_Owner' Label='Owner'/></DataColumns>"
        "<PrimaryKey ColumnNameID='Name'/><RowMap RowID='1' PageID='0' ShapeID='4'/></DataRecordSet></DataRecordSets>";
    const QByteArray rowset =
        "<xml xmlns:s=\"uuid:BDC6E3F0-6DA3-11d1-A2A3-00AA00C14882\" xmlns:dt=\"uuid:C2F41010-65B3-11d1-A29F-00AA00C14882\" "
        "xmlns:rs=\"urn:schemas-microsoft-com:rowset\" xmlns:z=\"#RowsetSchema\"><s:Schema id=\"RowsetSchema\">"
        "<s:ElementType name=\"row\" content=\"eltOnly\"><s:AttributeType name=\"c0\" rs:name=\"_Visio_RowID_\" rs:number=\"1\">"
        "<s:datatype dt:type=\"int\"/></s:AttributeType><s:AttributeType name=\"Name\" rs:number=\"2\">"
        "<s:datatype dt:type=\"string\"/></s:AttributeType><s:AttributeType name=\"Owner\" rs:number=\"3\">"
        "<s:datatype dt:type=\"string\"/></s:AttributeType></s:ElementType></s:Schema><rs:data>"
        "<z:row c0=\"1\" Name=\"Step\" Owner=\"Sam\"/><z:row c0=\"2\" Name=\"Review\" Owner=\"Ann\"/></rs:data></xml>";
    const QList<ExtraPart> extra{
        {str("visio/comments.xml"), comments, str("application/vnd.ms-visio.comments+xml"),
         str("http://schemas.microsoft.com/visio/2010/relationships/comments"), {}, {}},
        {str("visio/data/recordsets.xml"), recordsets, str("application/vnd.ms-visio.recordsets+xml"),
         str("http://schemas.microsoft.com/visio/2010/relationships/recordsets"), {}, {}},
        {str("visio/data/recordset1.xml"), rowset, str("application/vnd.ms-visio.recordset+xml"),
         str("http://schemas.microsoft.com/visio/2010/relationships/recordset"), str("visio/data/recordsets.xml"), str("rId1")}};
    return build({plan, back}, {}, {}, VisioFixture::smallTheme(), extra);
}

const Shape *withText(const Page &page, const char *text)
{
    for (const Shape &s : page.shapes) {
        if (s.text == str(text)) {
            return &s;
        }
    }
    return nullptr;
}

QMap<int, QString> shapeXml(const QByteArray &zip, const QString &part)
{
    QMap<int, QString> out;
    const XmlElementPtr root = Package::fromData(zip)->xml(part);
    for (const XmlElement *s : root->child(kNs, u"Shapes")->elements(kNs, u"Shape")) {
        out.insert(s->attribute({}, u"ID").toInt(), serializeFragment(*s));
    }
    return out;
}

QString pageSheet(const QByteArray &zip, int index)
{
    const XmlElementPtr pages = Package::fromData(zip)->xml(str("visio/pages/pages.xml"));
    return serializeFragment(*pages->elements(kNs, u"Page").at(index)->child(kNs, u"PageSheet"));
}

} // namespace

class DiagramM87VisioTest final : public QObject {
    Q_OBJECT
private slots:
    void readsVisiosOwnParts()
    {
        Visio::ReadResult r;
        QString error;
        QVERIFY2(Visio::readData(m87Package(), &r, &error), qPrintable(error));
        const Drawing &d = r.drawing;
        const Page &plan = d.pages.at(0);
        QCOMPARE(plan.layers.size(), 2);
        QVERIFY(!plan.layers.at(1).visible);
        QCOMPARE(plan.layers.at(1).color, QColor(0xc0, 0, 0));
        QVERIFY(!plan.layers.at(0).color.isValid());
        const Shape *alpha = withText(plan, "Alpha");
        const Shape *hidden = withText(plan, "Hidden note");
        QVERIFY(alpha != nullptr && hidden != nullptr); // a hidden layer's shape is read, not dropped
        QVERIFY(!itemVisible(plan, hidden->id));
        QCOMPARE(alpha->links.size(), 1);
        QCOMPARE(alpha->links.first().address, str("https://example.org/spec"));
        const Shape *lane = withText(plan, "Sales");
        QCOMPARE(lane->structure, str("lane"));
        QVERIFY(Ops::isLane(*lane));
        const Shape *step = withText(plan, "Step");
        QCOMPARE(Ops::containerMembers(plan, lane->id), QStringList{step->id});
        QCOMPARE(dataValue(step->data, str("Owner")), str("Sam"));
        QCOMPARE(d.recordsets.size(), 1);
        QCOMPARE(d.recordsets.first().columns, (QStringList{str("Name"), str("Owner")}));
        QCOMPARE(d.recordsets.first().rows.size(), 2);
        QCOMPARE(d.recordsets.first().keyColumn, 0);
        QCOMPARE(step->dataLink, (DataLink{d.recordsets.first().id, str("Step")}));
        QCOMPARE(plan.comments.size(), 2);
        QCOMPARE(plan.comments.at(0).itemId, alpha->id);
        QCOMPARE(plan.comments.at(0).author, str("Dana Brooks"));
        QVERIFY(plan.comments.at(1).done && plan.comments.at(1).itemId.isEmpty());
        QVERIFY(d.pages.at(1).backgroundPage);
        QCOMPARE(plan.backPageId, d.pages.at(1).id);
        QVERIFY(plan.lineJumps);
        QCOMPARE(plan.jumpOn, JumpOn::Horizontal);
        QCOMPARE(plan.jumpStyle, JumpStyle::Gap);
        QVERIFY(!plan.autoSize);
        QCOMPARE(plan.print.paper, QSizeF(1056, 816));
        QCOMPARE(plan.print.zoom, 1.0);
        QVERIFY(!plan.print.center);
        QVERIFY(r.warnings.join(QLatin1Char(' ')).indexOf(str("background")) < 0); // no longer approximate
    }

    void untouchedPartsTravelByteForByte()
    {
        const QByteArray source = m87Package();
        Visio::ReadResult r;
        QVERIFY(Visio::readData(source, &r));
        const QByteArray written = Visio::write(r.drawing, r.source.get(), Visio::Kind::Drawing);
        const auto in = Package::fromData(source);
        const auto out = Package::fromData(written);
        QCOMPARE(shapeXml(written, str("visio/pages/page1.xml")), shapeXml(source, str("visio/pages/page1.xml")));
        QCOMPARE(shapeXml(written, str("visio/pages/page2.xml")), shapeXml(source, str("visio/pages/page2.xml")));
        QCOMPARE(pageSheet(written, 0), pageSheet(source, 0));
        for (const QString &part : {str("visio/comments.xml"), str("visio/data/recordsets.xml"), str("visio/data/recordset1.xml")}) {
            QVERIFY2(out->contains(part), qPrintable(part));
            QCOMPARE(out->data(part), in->data(part));
        }
        const XmlElementPtr pages = out->xml(str("visio/pages/pages.xml"));
        QCOMPARE(pages->elements(kNs, u"Page").at(0)->attribute({}, u"BackPage"), str("1"));
        QCOMPARE(pages->elements(kNs, u"Page").at(1)->attribute({}, u"Background"), str("1"));
        // A renamed layer rewrites the Layer section only.
        Drawing d = r.drawing;
        Layer renamed = d.pages[0].layers.at(0);
        renamed.name = str("Process");
        QVERIFY(Ops::setLayer(d.pages[0], 0, renamed));
        QByteArray next = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        QVERIFY(pageSheet(next, 0).contains(str("V=\"Process\"")));
        QVERIFY(pageSheet(next, 0).contains(str("V=\"Notes\"")));
        QCOMPARE(shapeXml(next, str("visio/pages/page1.xml")), shapeXml(source, str("visio/pages/page1.xml")));
        QCOMPARE(Package::fromData(next)->data(str("visio/comments.xml")), in->data(str("visio/comments.xml")));
        // A new reply rewrites the comments part, keeping Visio's IDs.
        Ops::addComment(d, d.pages[0], {}, str("Agreed"), str("Sam"), str("S"),
                        QDateTime(QDate(2026, 10, 9), QTime(8, 0), QTimeZone::UTC), d.pages[0].comments.first().id);
        next = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        const XmlElementPtr comments = Package::fromData(next)->xml(str("visio/comments.xml"));
        const QList<const XmlElement *> entries = comments->child(kNs, u"CommentList")->elements(kNs, u"CommentEntry");
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0)->attribute({}, u"CommentID"), str("7"));
        QCOMPARE(entries.at(0)->attribute({}, u"ShapeID"), str("1"));
        QCOMPARE(entries.at(2)->text(), str("Agreed"));
        QCOMPARE(entries.at(2)->attribute({}, u"ShapeID"), str("1")); // the reply sits on Alpha
        QCOMPARE(comments->child(kNs, u"AuthorList")->elements().size(), 2);
        // Refreshed data rewrites the data parts and the linked rows.
        DataTable table = dataTableFromCsv(str("Name,Owner\nStep,Kim\nReview,Ann\n"));
        QVERIFY(Ops::refreshRecordset(d, d.recordsets.first().id, table, QDateTime::currentDateTimeUtc()));
        next = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        const auto changed = Package::fromData(next);
        QVERIFY(changed->data(str("visio/data/recordset1.xml")).contains("Kim"));
        const XmlElementPtr sets = changed->xml(str("visio/data/recordsets.xml"));
        const XmlElement *map = sets->child(kNs, u"DataRecordSet")->child(kNs, u"RowMap");
        QVERIFY(map != nullptr);
        QCOMPARE(map->attribute({}, u"ShapeID"), str("4"));
        QCOMPARE(map->attribute({}, u"RowID"), str("1"));
        QVERIFY(shapeXml(next, str("visio/pages/page1.xml")).value(4).contains(str("V=\"Kim\"")));
        Visio::ReadResult again;
        QVERIFY(Visio::readData(next, &again));
        QCOMPARE(dataValue(withText(again.drawing.pages.at(0), "Step")->data, str("Owner")), str("Kim"));
        QCOMPARE(withText(again.drawing.pages.at(0), "Step")->dataLink.row, str("Step"));
    }

    void freshDrawingCarriesItAll()
    {
        Drawing d = newDrawing();
        Page &p = d.pages[0];
        p.name = str("Flow");
        const QString lane = Ops::addShape(d, p, str("laneHorizontal"), {400, 175}, str("Volunteers"));
        p.shape(lane)->rect = QRectF(40, 100, 720, 150);
        Ops::insertLane(d, p, lane, true, {}, {});
        const QString step = Ops::addShape(d, p, str("process"), {300, 175}, str("Apply online"));
        Ops::addLayer(p, str("Flow"));
        Ops::addLayer(p, str("Draft"));
        p.layers[1].visible = false;
        p.layers[1].color = QColor(str("#2f6fb2"));
        Ops::assignLayers(p, {step}, {0});
        Ops::setLinks(p, {step}, {{str("Form"), str("https://example.org/form"), {}}});
        Ops::addComment(d, p, step, str("Shorten this"), str("Ann"), str("A"), QDateTime(QDate(2026, 5, 6), QTime(7, 8), QTimeZone::UTC));
        p.lineJumps = true;
        p.jumpStyle = JumpStyle::Square;
        p.jumpOn = JumpOn::Vertical;
        p.print = PrintSetup{QSizeF(1056, 816), 0, 2, 1, true};
        const QString data = Ops::addRecordset(d, dataTableFromCsv(str("Name,Owner\nApply online,Dana\n")), str("Steps"), {}, 0,
                                               QDateTime(QDate(2026, 5, 6), QTime(7, 8), QTimeZone::UTC));
        QCOMPARE(Ops::autoLink(d, p, data, 0, {}), 1);
        // Last: a new background page may move every page, `p` included.
        Ops::addBorderAndTitle(d, QString(p.id), str("Onboarding"));
        QString error;
        const QByteArray zip = Visio::write(d, nullptr, Visio::Kind::Drawing, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        const auto package = Package::fromData(zip);
        QCOMPARE(package->contentType(str("visio/comments.xml")), str("application/vnd.ms-visio.comments+xml"));
        QCOMPARE(package->contentType(str("visio/data/recordsets.xml")), str("application/vnd.ms-visio.recordsets+xml"));
        QCOMPARE(package->contentType(str("visio/data/recordset1.xml")), str("application/vnd.ms-visio.recordset+xml"));
        QVERIFY(package->firstOfType(str("visio/document.xml"), str("http://schemas.microsoft.com/visio/2010/relationships/comments")));
        QVERIFY(package->firstOfType(str("visio/data/recordsets.xml"), str("http://schemas.microsoft.com/visio/2010/relationships/recordset")));
        const QString sheet = pageSheet(zip, 0);
        for (const char *cell : {"N=\"LineJumpCode\" V=\"2\"", "N=\"LineJumpStyle\" V=\"3\"", "N=\"OnPage\" V=\"1\"",
                                 "N=\"PagesX\" V=\"2\"", "N=\"PaperKind\" V=\"1\"", "N=\"DrawingResizeType\" V=\"2\"",
                                 "N=\"NameUniv\" V=\"Draft\"", "N=\"Color\" V=\"#2f6fb2\""}) {
            QVERIFY2(sheet.contains(str(cell)), cell);
        }
        const QString shapes = QString::fromUtf8(Package::fromData(zip)->data(str("visio/pages/page1.xml")));
        for (const char *text : {"N=\"msvStructureType\"", "V=\"Swimlane;DoNotContain\"", "DEPENDSON(1,Sheet.",
                                 "DEPENDSON(4,Sheet.", "N=\"LayerMember\" V=\"0\"", "N=\"Hyperlink\"", "N=\"_VisDM_Owner\"",
                                 "N=\"DataLinked\" V=\"1\""}) {
            QVERIFY2(shapes.contains(str(text)), text);
        }
        QVERIFY(package->data(str("visio/data/recordset1.xml")).contains("rs:name=\"_Visio_RowID_\""));
        if (QindaOffice::Test::schemaValidationAvailable()) {
            const QStringList problems = QindaOffice::Test::validatePackage(*package);
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
        // Read back: the same model.
        Visio::ReadResult back;
        QVERIFY(Visio::readData(zip, &back, &error));
        const Page &b = back.drawing.pages.at(0);
        QCOMPARE(b.layers.size(), 2);
        QCOMPARE(b.layers.at(1).name, str("Draft"));
        QVERIFY(!b.layers.at(1).visible);
        QCOMPARE(b.layers.at(1).color.rgb(), QColor(str("#2f6fb2")).rgb());
        QCOMPARE(b.jumpStyle, JumpStyle::Square);
        QCOMPARE(b.jumpOn, JumpOn::Vertical);
        QCOMPARE(b.print.sheetsAcross, 2);
        QCOMPARE(b.print.paper, QSizeF(1056, 816));
        QCOMPARE(b.comments.size(), 1);
        QCOMPARE(b.comments.first().text, str("Shorten this"));
        QVERIFY(back.drawing.pages.last().backgroundPage);
        QCOMPARE(b.backPageId, back.drawing.pages.last().id);
        const Shape *readStep = withText(b, "Apply online");
        QCOMPARE(readStep->layers, QList<int>{0});
        QCOMPARE(readStep->links.size(), 1);
        QVERIFY(readStep->dataLink.linked());
        QCOMPARE(dataValue(readStep->data, str("Owner")), str("Dana"));
        QCOMPARE(withText(b, "Volunteers")->structure, str("lane"));
        QCOMPARE(Ops::poolOf(b, withText(b, "Volunteers")->id).size(), 2);
    }

    void libreOfficeRendersIt()
    {
        if (QStandardPaths::findExecutable(str("soffice")).isEmpty() || QStandardPaths::findExecutable(str("pdftotext")).isEmpty()) {
            QSKIP("LibreOffice (soffice) and pdftotext are needed for the external check");
        }
        Visio::ReadResult r;
        QVERIFY(Visio::readData(m87Package(), &r));
        Drawing d = r.drawing;
        Page &p = d.pages[0];
        const QString added = Ops::insertLane(d, p, withText(p, "Sales")->id, true, {}, {});
        Ops::setText(p, added, str("Support"));
        Ops::addComment(d, p, {}, str("A new note"), str("Sam"), {}, QDateTime::currentDateTimeUtc());
        QTemporaryDir dir;
        const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
        const QString out = grabs.isEmpty() ? dir.path() : grabs;
        const QString vsdx = QDir(out).filePath(str("visio_m87.vsdx"));
        QFile file(vsdx);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(Visio::write(d, r.source.get(), Visio::Kind::Drawing));
        file.close();
        QProcess soffice;
        QindaOffice::Test::startSoffice(soffice, {str("--headless"), str("--norestore"), str("--nologo"),
                                                  str("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                                                  str("--convert-to"), str("pdf"), str("--outdir"), out, vsdx});
        QVERIFY(soffice.waitForFinished(180000));
        const QString pdf = QDir(out).filePath(str("visio_m87.pdf"));
        QVERIFY2(QFile::exists(pdf), "LibreOffice could not convert the export");
        QProcess text;
        text.start(str("pdftotext"), {pdf, str("-")});
        QVERIFY(text.waitForFinished(60000));
        const QString words = QString::fromUtf8(text.readAllStandardOutput()).simplified();
        for (const char *expect : {"Alpha", "Sales", "Support", "Step", "Title block"}) {
            QVERIFY2(words.contains(str(expect)), qPrintable(str(expect) + str(" missing in ") + words));
        }
    }
};

QTEST_MAIN(DiagramM87VisioTest)
#include "tst_diagram_m87_visio.moc"
