// SPDX-License-Identifier: GPL-3.0-or-later
// Visio preservation (M65, D-073 / D-068 rule 3): a drawing read from a
// package and written back unchanged is the package's own XML — every
// shape element, the connects, masters, theme, document and the parts the
// model knows nothing of; an edited drawing rewrites only the mapped cells
// of what changed, keeps IDs unique and glue consistent, moves a group as
// a whole, and survives a page resize. QQO_VISIO_CORPUS (':'-separated
// .vsdx paths) runs the same round trips over real drawings, reporting
// aggregate counts only; QQO_GRAB_DIR keeps PNGs of their pages.

#include "export.h"
#include "ops.h"
#include "visio_fixture.h"
#include "visio_io.h"
#include "visio_source.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QDir>
#include <QFileInfo>
#include <QtTest>

using namespace QindaOffice::Diagram;
using namespace QindaOffice::Ooxml;
using VisioFixture::str;

namespace {

const QString kNs = QStringLiteral("http://schemas.microsoft.com/office/visio/2012/main");

XmlElementPtr partXml(const QByteArray &zip, const QString &part)
{
    const auto package = Package::fromData(zip);
    return package ? package->xml(part) : nullptr;
}

// The top-level <Shape> elements of a page part, by ID.
QMap<int, XmlElementPtr> shapesOf(const QByteArray &zip, const QString &part)
{
    QMap<int, XmlElementPtr> out;
    const XmlElementPtr root = partXml(zip, part);
    const XmlElement *shapes = root ? root->child(kNs, u"Shapes") : nullptr;
    if (shapes != nullptr) {
        for (const XmlChild &c : shapes->children) {
            if (c.element && c.element->is(kNs, u"Shape")) {
                out.insert(c.element->attribute({}, u"ID").toInt(), c.element);
            }
        }
    }
    return out;
}

QStringList shapeOrder(const QByteArray &zip, const QString &part)
{
    QStringList ids;
    const XmlElementPtr root = partXml(zip, part);
    if (const XmlElement *shapes = root ? root->child(kNs, u"Shapes") : nullptr) {
        for (const XmlElement *s : shapes->elements(kNs, u"Shape")) {
            ids.append(s->attribute({}, u"ID"));
        }
    }
    return ids;
}

QString cellValue(const XmlElement &shape, const QString &name)
{
    for (const XmlElement *c : shape.elements(kNs, u"Cell")) {
        if (c->attribute({}, u"N") == name) {
            return c->attribute({}, u"V");
        }
    }
    return {};
}

// Every child of `shape` but the cells named in `except`, serialized.
QStringList childrenExcept(const XmlElement &shape, const QStringList &except)
{
    QStringList out;
    for (const XmlElement *c : shape.elements()) {
        if (c->is(kNs, u"Cell") && except.contains(c->attribute({}, u"N"))) {
            continue;
        }
        out.append(serializeFragment(*c));
    }
    return out;
}

QList<QPair<int, int>> connects(const QByteArray &zip, const QString &part)
{
    QList<QPair<int, int>> out;
    const XmlElementPtr root = partXml(zip, part);
    if (const XmlElement *c = root ? root->child(kNs, u"Connects") : nullptr) {
        for (const XmlElement *row : c->elements(kNs, u"Connect")) {
            out.append({row->attribute({}, u"FromSheet").toInt(), row->attribute({}, u"ToSheet").toInt()});
        }
    }
    return out;
}

void collectIds(const XmlElement &e, QList<int> &ids)
{
    for (const XmlElement *s : e.elements(kNs, u"Shape")) {
        ids.append(s->attribute({}, u"ID").toInt());
        if (const XmlElement *nested = s->child(kNs, u"Shapes")) {
            collectIds(*nested, ids);
        }
    }
}

// Visio's repair rules (visio_write_p.h): unique IDs at every depth, and
// every Connect naming IDs on its page.
void verifyPageRules(const QByteArray &zip, const QString &part)
{
    const XmlElementPtr root = partXml(zip, part);
    QVERIFY(root);
    QList<int> ids;
    if (const XmlElement *shapes = root->child(kNs, u"Shapes")) {
        collectIds(*shapes, ids);
    }
    const QSet<int> unique(ids.cbegin(), ids.cend());
    QCOMPARE(unique.size(), ids.size());
    for (const auto &[from, to] : connects(zip, part)) {
        QVERIFY2(unique.contains(from) && unique.contains(to), qPrintable(QStringLiteral("%1 → %2").arg(from).arg(to)));
    }
}

const Shape *shapeWithText(const Page &page, const QString &text)
{
    for (const Shape &s : page.shapes) {
        if (s.text == text) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace

class DiagramVisioRoundTripTest final : public QObject {
    Q_OBJECT
private slots:
    void unchangedDrawingIsWrittenAsItWas()
    {
        const QByteArray source = VisioFixture::officePackage();
        Visio::ReadResult r;
        QVERIFY(Visio::readData(source, &r));
        QCOMPARE(r.drawing.pages.size(), 2);
        QCOMPARE(r.drawing.pages.first().shapes.size(), 7); // 1–4, 6, 7, 10: no shape for the group or the guide
        QCOMPARE(r.drawing.pages.first().connectors.size(), 1);
        const QByteArray written = Visio::write(r.drawing, r.source.get(), Visio::Kind::Drawing);
        QVERIFY(!written.isEmpty());
        for (const QString &part : {str("visio/pages/page1.xml"), str("visio/pages/page2.xml")}) {
            const QMap<int, XmlElementPtr> before = shapesOf(source, part);
            const QMap<int, XmlElementPtr> after = shapesOf(written, part);
            QCOMPARE(after.keys(), before.keys());
            for (auto it = before.cbegin(); it != before.cend(); ++it) {
                QCOMPARE(serializeFragment(*after.value(it.key())), serializeFragment(**it));
            }
            QCOMPARE(shapeOrder(written, part), shapeOrder(source, part)); // stacking order kept
            QCOMPARE(connects(written, part), connects(source, part));
            verifyPageRules(written, part);
        }
        const auto in = Package::fromData(source);
        const auto out = Package::fromData(written);
        for (const QString &part : {str("visio/masters/masters.xml"), str("visio/masters/master1.xml"),
                                    str("visio/theme/theme1.xml"), str("visio/validation.xml")}) {
            QVERIFY2(out->contains(part), qPrintable(part));
            QCOMPARE(out->data(part), in->data(part));
        }
        QCOMPARE(serializeFragment(*out->xml(str("visio/document.xml"))),
                 serializeFragment(*in->xml(str("visio/document.xml"))));
        QVERIFY(out->data(str("docProps/custom.xml")).contains("RecalcDocument"));
        // pages.xml: each Page entry with its PageSheet (layers) as it was.
        const XmlElementPtr pages = out->xml(str("visio/pages/pages.xml"));
        const XmlElement *plan = pages->child(kNs, u"Page");
        QCOMPARE(plan->attribute({}, u"Name"), str("Plan"));
        QVERIFY(serializeFragment(*plan->child(kNs, u"PageSheet")).contains(str("Flow")));
    }

    void editsRewriteOnlyWhatChanged()
    {
        const QByteArray source = VisioFixture::officePackage();
        Visio::ReadResult r;
        QVERIFY(Visio::readData(source, &r));
        Drawing d = r.drawing;
        Page &p = d.pages[0];
        const QString alpha = shapeWithText(p, str("Alpha"))->id;
        const QString beta = shapeWithText(p, str("Beta"))->id;
        const QString gamma = shapeWithText(p, str("Gamma"))->id;
        const QString delta = shapeWithText(p, str("Delta"))->id;
        const QString epsilon = shapeWithText(p, str("Epsilon"))->id;
        QVERIFY(Ops::moveItems(p, {alpha}, QPointF(48, 0))); // half an inch right
        QVERIFY(Ops::setText(p, beta, str("Beta 2")));
        Ops::ShapeStylePatch green;
        green.fill = QColor(str("#00aa00"));
        QVERIFY(Ops::applyShapeStyle(p, {gamma}, green));
        QVERIFY(Ops::removeItems(p, {delta}) == 1);
        const QString fresh = Ops::addShape(d, p, str("process"), QPointF(500, 650), str("New"));
        QVERIFY(!Ops::addConnector(d, p, {fresh, 0, {}}, {alpha, -1, {}}).isEmpty());
        const QStringList copies = Ops::duplicate(d, p, {epsilon}, QPointF(24, 24));
        QCOMPARE(copies.size(), 1);
        const QByteArray written = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        const QString part = str("visio/pages/page1.xml");
        verifyPageRules(written, part);
        const QMap<int, XmlElementPtr> before = shapesOf(source, part);
        const QMap<int, XmlElementPtr> after = shapesOf(written, part);
        // Alpha moved: only its pin changed; master, user cells, link kept.
        QCOMPARE(childrenExcept(*after.value(1), {str("PinX")}), childrenExcept(*before.value(1), {str("PinX")}));
        QCOMPARE(cellValue(*after.value(1), str("PinX")), str("2.5"));
        QCOMPARE(after.value(1)->attribute({}, u"Master"), str("1"));
        // Beta: new text; its data and connection points kept.
        const QString betaXml = serializeFragment(*after.value(2));
        QVERIFY(betaXml.contains(str("Beta 2")));
        QVERIFY(betaXml.contains(str("Owner")) && betaXml.contains(str("Connection")));
        QCOMPARE(cellValue(*after.value(3), str("FillForegnd")), str("#00aa00"));
        QVERIFY(!after.contains(4));                                       // Delta deleted
        QCOMPARE(serializeFragment(*after.value(5)), serializeFragment(*before.value(5))); // the group, untouched
        QCOMPARE(serializeFragment(*after.value(8)), serializeFragment(*before.value(8))); // the guide, kept
        // The connector glued to Alpha follows it: new ends, same glue.
        const XmlElementPtr connector = after.value(9);
        QVERIFY(connector->attribute({}, u"ID") == str("9"));
        QCOMPARE(cellValue(*connector, str("BeginX")), str("3.5"));
        QVERIFY(serializeFragment(*connector).contains(str("_XFTRIGGER(Sheet.1!EventXFMod)")));
        // New items take fresh IDs and names; a copy is a new shape.
        QList<int> added;
        for (const int id : after.keys()) {
            if (!before.contains(id)) {
                added.append(id);
                QVERIFY(id > 10);
                QVERIFY(!after.value(id)->hasAttribute({}, u"UniqueID"));
                QVERIFY(after.value(id)->attribute({}, u"NameU").endsWith(QLatin1Char('.') + QString::number(id)));
            }
        }
        QCOMPARE(added.size(), 3); // the process, its connector, the copy
        const QList<QPair<int, int>> rows = connects(written, part);
        QVERIFY(rows.contains({9, 1}) && rows.contains({9, 2}));
        QCOMPARE(std::count_if(rows.cbegin(), rows.cend(), [&](const QPair<int, int> &row) { return added.contains(row.first); }), 2L);
        // And the written file reads back as the edited drawing.
        Visio::ReadResult back;
        QVERIFY(Visio::readData(written, &back));
        const Page &q = back.drawing.pages.first();
        QCOMPARE(q.shapes.size(), p.shapes.size());
        QCOMPARE(q.connectors.size(), p.connectors.size());
        QVERIFY(shapeWithText(q, str("Beta 2")) != nullptr && shapeWithText(q, str("New")) != nullptr);
        QVERIFY(QLineF(shapeWithText(q, str("Alpha"))->rect.center(), shapeWithText(p, str("Alpha"))->rect.center()).length() < 0.5);
        QVERIFY(QLineF(shapeWithText(q, str("New"))->rect.center(), shapeWithText(p, str("New"))->rect.center()).length() < 0.5);
        for (const Connector &c : q.connectors) {
            QVERIFY(c.from.glued() && c.to.glued());
        }
    }

    void movedGroupMovesAsOne()
    {
        const QByteArray source = VisioFixture::officePackage();
        Visio::ReadResult r;
        QVERIFY(Visio::readData(source, &r));
        Drawing d = r.drawing;
        Page &p = d.pages[0];
        const QString g1 = shapeWithText(p, str("G1"))->id;
        QVERIFY(Ops::moveItems(p, Ops::expandGroups(p, {g1}), QPointF(0, 96))); // an inch down
        const QByteArray written = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        const XmlElementPtr before = shapesOf(source, str("visio/pages/page1.xml")).value(5);
        const XmlElementPtr after = shapesOf(written, str("visio/pages/page1.xml")).value(5);
        QCOMPARE(cellValue(*after, str("PinY")), str("2"));
        QCOMPARE(childrenExcept(*after, {str("PinY")}), childrenExcept(*before, {str("PinY")}));
    }

    void resizedPageKeepsShapesInPlace()
    {
        Visio::ReadResult r;
        QVERIFY(Visio::readData(VisioFixture::officePackage(), &r));
        Drawing d = r.drawing;
        d.pages[0].size.setHeight(d.pages[0].size.height() + 192);
        const QByteArray written = Visio::write(d, r.source.get(), Visio::Kind::Drawing);
        verifyPageRules(written, str("visio/pages/page1.xml"));
        Visio::ReadResult back;
        QVERIFY(Visio::readData(written, &back));
        const Page &p = d.pages.first();
        const Page &q = back.drawing.pages.first();
        QCOMPARE(q.size, p.size);
        for (const QString &text : {str("Alpha"), str("Beta"), str("G1"), str("G2"), str("Epsilon")}) {
            QVERIFY2(QLineF(shapeWithText(q, text)->rect.center(), shapeWithText(p, text)->rect.center()).length() < 0.5,
                     qPrintable(text));
        }
    }

    void corpusRoundTrips()
    {
        const QStringList files = qEnvironmentVariable("QQO_VISIO_CORPUS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (files.isEmpty()) {
            QSKIP("Set QQO_VISIO_CORPUS to ':'-separated .vsdx paths to round-trip real drawings.");
        }
        const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
        int pages = 0;
        int shapes = 0;
        int connectorsCount = 0;
        int failures = 0;
        for (const QString &file : files) {
            Visio::ReadResult r;
            QString error;
            if (!Visio::read(file, &r, &error)) {
                ++failures;
                qWarning("read failed: %s", qPrintable(error));
                continue;
            }
            const QByteArray written = Visio::write(r.drawing, r.source.get(), Visio::Kind::Drawing, &error);
            Visio::ReadResult back;
            if (written.isEmpty() || !Visio::readData(written, &back, &error)) {
                ++failures;
                continue;
            }
            for (int i = 0; i < r.drawing.pages.size(); ++i) {
                const Page &a = r.drawing.pages.at(i);
                const Page &b = back.drawing.pages.at(i);
                failures += a.shapes.size() != b.shapes.size() || a.connectors.size() != b.connectors.size() ? 1 : 0;
                verifyPageRules(written, QStringLiteral("visio/pages/page%1.xml").arg(i + 1));
                pages += 1;
                shapes += int(a.shapes.size());
                connectorsCount += int(a.connectors.size());
                if (!grabs.isEmpty()) {
                    QString exportError;
                    exportPng(QDir(grabs).filePath(QStringLiteral("visio_corpus_%1_%2.png").arg(QFileInfo(file).completeBaseName()).arg(i + 1)),
                              a, {}, 1.0, false, &exportError);
                }
            }
        }
        qInfo("Visio corpus: %lld files, %d pages, %d shapes, %d connectors, %d failures", qlonglong(files.size()), pages,
              shapes, connectorsCount, failures);
        QCOMPARE(failures, 0);
    }
};

QTEST_MAIN(DiagramVisioRoundTripTest)
#include "tst_diagram_visio_roundtrip.moc"
