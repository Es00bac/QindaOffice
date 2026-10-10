// SPDX-License-Identifier: GPL-3.0-or-later
// Visio export (M65, D-073): a drawing made in QindaDiagram — stencils,
// formatted text, shape data, a picture, every connector kind, a group, two
// pages — becomes a package Visio opens without repair (content types,
// relationships, the standard style sheets, unique IDs, connects naming
// shapes on their page, RecalcDocument) and reads back as the same drawing;
// templates and macro-enabled kinds; DrawingDocument opens, saves and
// recovers Visio files; and, when LibreOffice is installed, it renders the
// export with every text (QQO_GRAB_DIR keeps the PDF and a PNG).

#include "drawing_document.h"
#include "geometry.h"
#include "ops.h"
#include "stencils.h"
#include "visio_fixture.h"
#include "visio_io.h"

#include "ooxml_package.h"
#include "support/soffice_env.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Diagram;
using namespace QindaOffice::Ooxml;
using VisioFixture::str;

namespace {

const QString kNs = QStringLiteral("http://schemas.microsoft.com/office/visio/2012/main");

QByteArray png(int w, int h, const QColor &color)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

Drawing freshDrawing()
{
    Drawing d = newDrawing();
    Page &p = d.pages[0];
    p.name = str("Flow");
    const QString start = Ops::addShape(d, p, str("terminator"), {150, 120}, str("Start"));
    const QString step = Ops::addShape(d, p, str("process"), {150, 270}, str("Review the request"));
    const QString choice = Ops::addShape(d, p, str("decision"), {150, 440}, str("Approved?"));
    const QString note = Ops::addShape(d, p, str("note"), {420, 120}, str("Call within 3 days"));
    const QString server = Ops::addShape(d, p, str("server"), {440, 300}, str("File server"));
    Ops::addShape(d, p, str("umlActor"), {620, 300}, str("Clerk"));
    Ops::addShape(d, p, str("text"), {440, 460}, str("Plain words"));
    Ops::addShape(d, p, str("container"), {820, 520}, str("Team"));
    Ops::TextStylePatch text;
    text.family = str("Arial");
    text.size = 14;
    text.bold = true;
    text.color = QColor(str("#c00000"));
    text.horizontal = Qt::AlignLeft;
    text.vertical = Qt::AlignTop;
    Ops::applyTextStyle(p, {step}, text);
    Ops::setData(p, server, str("IP address"), str("10.0.0.5"));
    Ops::rotate(p, {choice}, 30);
    Ops::flip(p, {note}, Qt::Horizontal);
    const QString picture = Ops::addShape(d, p, str("picture"), {700, 120}, QString());
    p.shape(picture)->image = png(8, 4, Qt::darkCyan);
    p.shape(picture)->rect = QRectF(640, 90, 120, 60);
    Ops::addConnector(d, p, {start, 2, {}}, {step, 0, {}});
    Ops::addConnector(d, p, {step, -1, {}}, {choice, -1, {}}, Routing::Straight);
    const QString curved = Ops::addConnector(d, p, {choice, 1, {}}, {server, 3, {}}, Routing::Curved);
    Ops::setText(p, curved, str("yes"));
    Ops::ConnectorPatch arrows;
    arrows.startArrow = ArrowStyle::Diamond;
    arrows.endArrow = ArrowStyle::Open;
    Ops::applyConnectorPatch(p, {curved}, arrows);
    Ops::addConnector(d, p, {{}, -1, {600, 520}}, {{}, -1, {700, 580}}, Routing::Straight);
    const QString a = Ops::addShape(d, p, str("ellipse"), {300, 620}, str("A"));
    const QString b = Ops::addShape(d, p, str("ellipse"), {420, 620}, str("B"));
    Ops::group(d, p, {a, b});
    Ops::addPage(d, 1, str("Second"));
    Page &second = d.pages[1];
    second.size = QSizeF(816, 1056);
    Ops::addShape(d, second, str("hexagon"), {400, 500}, str("Text on page two"));
    return d;
}

bool near(QPointF a, QPointF b, double tolerance = 0.5)
{
    return QLineF(a, b).length() <= tolerance;
}

bool visible(const QColor &c)
{
    return c.isValid() && c.alpha() > 0;
}

int indexOf(const Page &page, const QString &id)
{
    return page.shapeIndex(id);
}

} // namespace

class DiagramVisioWriteTest final : public QObject {
    Q_OBJECT
private slots:
    void freshDrawingWritesAValidPackage()
    {
        const Drawing d = freshDrawing();
        QString error;
        const QByteArray zip = Visio::write(d, nullptr, Visio::Kind::Drawing, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        const auto package = Package::fromData(zip);
        QVERIFY(package);
        QCOMPARE(package->contentType(str("visio/document.xml")), str("application/vnd.ms-visio.drawing.main+xml"));
        QCOMPARE(package->contentType(str("visio/pages/pages.xml")), str("application/vnd.ms-visio.pages+xml"));
        QCOMPARE(package->contentType(str("visio/pages/page2.xml")), str("application/vnd.ms-visio.page+xml"));
        QCOMPARE(package->contentType(str("visio/windows.xml")), str("application/vnd.ms-visio.windows+xml"));
        QVERIFY(package->firstOfType(QString(), str("http://schemas.microsoft.com/visio/2010/relationships/document")));
        QVERIFY(package->data(str("docProps/custom.xml")).contains("RecalcDocument"));
        // The style sheets Visio expects: No Style, Text Only, None, Normal,
        // Guide — and the Connector style the connectors use.
        QStringList styles;
        const XmlElementPtr document = package->xml(str("visio/document.xml"));
        for (const XmlElement *s : document->child(kNs, u"StyleSheets")->elements()) {
            styles.append(s->attribute({}, u"NameU"));
        }
        QCOMPARE(styles, QStringList({str("No Style"), str("Text Only"), str("None"), str("Normal"), str("Guide"),
                                      str("Connector")}));
        // Each shape lists its cells before its sections, as Visio writes.
        for (const QString &part : {str("visio/pages/page1.xml"), str("visio/pages/page2.xml")}) {
            const XmlElementPtr root = package->xml(part);
            QList<const XmlElement *> shapes = root->child(kNs, u"Shapes")->elements(kNs, u"Shape");
            while (!shapes.isEmpty()) {
                const XmlElement *shape = shapes.takeFirst();
                bool sectionSeen = false;
                for (const XmlElement *c : shape->elements()) {
                    QVERIFY2(!(sectionSeen && c->is(kNs, u"Cell")), qPrintable(shape->attribute({}, u"NameU")));
                    sectionSeen = sectionSeen || c->is(kNs, u"Section");
                }
                if (const XmlElement *nested = shape->child(kNs, u"Shapes")) {
                    shapes.append(nested->elements(kNs, u"Shape"));
                }
            }
        }
        // Element names are unprefixed (libvisio matches them literally).
        const QByteArray page1 = package->data(str("visio/pages/page1.xml"));
        QVERIFY(page1.contains("<PageContents xmlns=\"http://schemas.microsoft.com/office/visio/2012/main\""));
        QVERIFY(!page1.contains("ns0:"));
        QVERIFY(page1.contains("r:id="));
        // The picture's bytes travel as a media part.
        const auto media = package->relationships(str("visio/pages/page1.xml"));
        QVERIFY(std::any_of(media.cbegin(), media.cend(), [&](const Relationship &rel) {
            return package->contentType(package->targetPart(str("visio/pages/page1.xml"), rel)) == str("image/png");
        }));
        Visio::ReadResult back;
        QVERIFY(Visio::readData(zip, &back, &error));
        QCOMPARE(back.drawing.pages.size(), 2);
        for (int pi = 0; pi < 2; ++pi) {
            const Page &a = d.pages.at(pi);
            const Page &b = back.drawing.pages.at(pi);
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.size, a.size);
            QCOMPARE(b.shapes.size(), a.shapes.size());
            QCOMPARE(b.connectors.size(), a.connectors.size());
            for (int i = 0; i < a.shapes.size(); ++i) {
                const Shape &x = a.shapes.at(i);
                const Shape &y = b.shapes.at(i);
                QVERIFY2(near(x.rect.center(), y.rect.center()), qPrintable(x.type));
                QVERIFY2(std::abs(x.rect.width() - y.rect.width()) < 0.5 && std::abs(x.rect.height() - y.rect.height()) < 0.5,
                         qPrintable(x.type));
                // The same placement: compare where the local corners land.
                for (const QPointF corner : {QPointF(0, 0), QPointF(x.rect.width(), 0), QPointF(0, x.rect.height())}) {
                    QVERIFY2(near(shapeTransform(x).map(corner), shapeTransform(y).map(corner)), qPrintable(x.type));
                }
                QCOMPARE(y.text, x.text);
                if (!x.text.isEmpty()) {
                    QCOMPARE(y.textStyle.family, x.textStyle.family);
                    QVERIFY(std::abs(y.textStyle.size - x.textStyle.size) < 1e-6);
                    QCOMPARE(y.textStyle.bold, x.textStyle.bold);
                    QCOMPARE(y.textStyle.color.rgb(), x.textStyle.color.rgb());
                    QCOMPARE(y.textStyle.align, x.textStyle.align);
                    const QRectF tx = textRect(x);
                    const QRectF ty = textRect(y);
                    QVERIFY2(near(tx.center(), ty.center()) && std::abs(tx.width() - ty.width()) < 0.5, qPrintable(x.type));
                }
                QCOMPARE(y.data, x.data);
                QCOMPARE(y.groupId.isEmpty(), x.groupId.isEmpty());
                if (x.type == QLatin1String("picture")) {
                    QCOMPARE(y.type, str("picture"));
                    QCOMPARE(QImage::fromData(y.image).size(), QSize(8, 4));
                    continue;
                }
                if (paintsOutline(x) && visible(x.style.fill)) {
                    QCOMPARE(y.style.fill.rgb(), x.style.fill.rgb());
                }
                QCOMPARE(visible(y.style.line), visible(x.style.line));
                if (visible(x.style.line)) {
                    QCOMPARE(y.style.line.rgb(), x.style.line.rgb());
                }
                QVERIFY(std::abs(y.style.lineWidth - x.style.lineWidth) < 1e-6);
                // The outline drawn the same: compare its page bounds.
                const QRectF ox = pageOutline(x).boundingRect();
                const QRectF oy = pageOutline(y).boundingRect();
                QVERIFY2(near(ox.topLeft(), oy.topLeft(), 1.0) && near(ox.bottomRight(), oy.bottomRight(), 1.0),
                         qPrintable(x.type));
            }
            for (int i = 0; i < a.connectors.size(); ++i) {
                const Connector &x = a.connectors.at(i);
                const Connector &y = b.connectors.at(i);
                QCOMPARE(y.routing, x.routing);
                QCOMPARE(y.startArrow, x.startArrow);
                QCOMPARE(y.endArrow, x.endArrow);
                QCOMPARE(y.text, x.text);
                QCOMPARE(y.line.rgb(), x.line.rgb());
                for (const bool begin : {true, false}) {
                    const Endpoint &ex = begin ? x.from : x.to;
                    const Endpoint &ey = begin ? y.from : y.to;
                    QCOMPARE(ey.glued(), ex.glued());
                    if (ex.glued()) {
                        QCOMPARE(indexOf(b, ey.shapeId), indexOf(a, ex.shapeId));
                        QCOMPARE(ey.port, ex.port);
                    } else {
                        QVERIFY(near(ey.point, ex.point));
                    }
                }
            }
        }
    }

    void templatesAndMacroKinds()
    {
        const Drawing d = freshDrawing();
        const auto type = [](const QByteArray &zip) { return Package::fromData(zip)->contentType(str("visio/document.xml")); };
        QCOMPARE(type(Visio::write(d, nullptr, Visio::Kind::Template)), str("application/vnd.ms-visio.template.main+xml"));
        QCOMPARE(Visio::kindForPath(str("x.vsdm")), Visio::Kind::MacroDrawing);
        // A macro-enabled source keeps its VBA project only as .vsdm/.vstm.
        const VisioFixture::ExtraPart vba{str("visio/vbaProject.bin"), QByteArray("VBA\0\1\2", 6),
                                          str("application/vnd.ms-office.vbaProject"),
                                          str("http://schemas.microsoft.com/visio/2010/relationships/vbaProject"), {}, {}};
        Visio::ReadResult r;
        QVERIFY(Visio::readData(VisioFixture::officePackage({vba}), &r));
        const auto macro = Package::fromData(Visio::write(r.drawing, r.source.get(), Visio::Kind::MacroDrawing));
        QVERIFY(macro->contains(str("visio/vbaProject.bin")));
        QCOMPARE(macro->contentType(str("visio/document.xml")), str("application/vnd.ms-visio.drawing.macroEnabled.main+xml"));
        const auto plain = Package::fromData(Visio::write(r.drawing, r.source.get(), Visio::Kind::Drawing));
        QVERIFY(!plain->contains(str("visio/vbaProject.bin")));
    }

    void drawingDocumentOpensSavesAndRecovers()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(str("plan.vsdx"));
        {
            QFile f(source);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(VisioFixture::officePackage());
        }
        DrawingDocument doc;
        QString error;
        QVERIFY2(doc.load(source, &error), qPrintable(error));
        QVERIFY(doc.hasVisioSource());
        QCOMPARE(doc.drawing().pages.size(), 2);
        const QString copy = dir.filePath(str("copy.vsdx"));
        QVERIFY2(doc.save(copy, &error), qPrintable(error));
        const auto original = Package::load(source);
        const auto saved = Package::load(copy);
        QCOMPARE(serializeFragment(*saved->xml(str("visio/pages/page2.xml"))),
                 serializeFragment(*original->xml(str("visio/pages/page2.xml"))));
        // Crash recovery restores the drawing without the package; a Visio
        // save finds it again at the document's path (it has not changed).
        DrawingDocument recovered;
        QVERIFY(recovered.loadRecovery(doc.recoverySnapshot(), &error));
        recovered.adoptPath(source);
        QVERIFY(!recovered.hasVisioSource());
        const QString after = dir.filePath(str("after.vsdx"));
        QVERIFY2(recovered.save(after, &error), qPrintable(error));
        QVERIFY(recovered.hasVisioSource());
        QCOMPARE(serializeFragment(*Package::load(after)->xml(str("visio/pages/page2.xml"))),
                 serializeFragment(*original->xml(str("visio/pages/page2.xml"))));
        // The old binary formats: a clear refusal both ways.
        QVERIFY(!doc.save(dir.filePath(str("old.vsd")), &error));
        QVERIFY(error.contains(str(".vsdx")));
        QVERIFY(!doc.load(dir.filePath(str("old.vsd")), &error));
        QVERIFY(error.contains(str("2003")));
        QVERIFY(doc.mimeTypes().contains(str("application/vnd.ms-visio.drawing.main+xml")));
    }

    void libreOfficeRendersTheExport()
    {
        if (QStandardPaths::findExecutable(str("soffice")).isEmpty()
            || QStandardPaths::findExecutable(str("pdftotext")).isEmpty()) {
            QSKIP("LibreOffice (soffice) and pdftotext are needed for the external check");
        }
        QTemporaryDir dir;
        const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
        const QString out = grabs.isEmpty() ? dir.path() : grabs;
        Visio::ReadResult office;
        QVERIFY(Visio::readData(VisioFixture::officePackage(), &office));
        Drawing edited = office.drawing;
        Ops::setText(edited.pages[0], edited.pages[0].shapes.at(1).id, str("Beta edited"));
        const QList<QPair<QString, QByteArray>> files{
            {str("visio_export_fresh"), Visio::write(freshDrawing(), nullptr, Visio::Kind::Drawing)},
            {str("visio_export_edited"), Visio::write(edited, office.source.get(), Visio::Kind::Drawing)}};
        const QMap<QString, QStringList> expected{
            {str("visio_export_fresh"), {str("Review the request"), str("File server"), str("Clerk"),
                                         str("Plain words"), str("Team"), str("yes"), str("Text on page two")}},
            {str("visio_export_edited"), {str("Alpha"), str("Beta edited"), str("Gamma"), str("G1"), str("Epsilon"),
                                          str("Note")}}};
        for (const auto &[name, zip] : files) {
            const QString vsdx = QDir(out).filePath(name + str(".vsdx"));
            QFile f(vsdx);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(zip);
            f.close();
            for (const QString &format : {str("pdf"), str("png")}) {
                QProcess soffice;
                QindaOffice::Test::startSoffice(
                    soffice, {str("--headless"), str("--norestore"), str("--nologo"),
                              str("-env:UserInstallation=file://%1/profile").arg(dir.path()), str("--convert-to"), format,
                              str("--outdir"), out, vsdx});
                QVERIFY(soffice.waitForFinished(180000));
            }
            const QString pdf = QDir(out).filePath(name + str(".pdf"));
            QVERIFY2(QFile::exists(pdf), "LibreOffice could not convert the export");
            QProcess text;
            text.start(str("pdftotext"), {pdf, str("-")});
            QVERIFY(text.waitForFinished(60000));
            const QString words = QString::fromUtf8(text.readAllStandardOutput()).simplified();
            for (const QString &expect : expected.value(name)) {
                QVERIFY2(words.contains(expect), qPrintable(name + str(": ") + expect + str(" missing in ") + words));
            }
        }
    }
};

QTEST_MAIN(DiagramVisioWriteTest)
#include "tst_diagram_visio_write.moc"
