// SPDX-License-Identifier: GPL-3.0-or-later
// diagram_engine model, file format and document (M50): ids never repeat,
// paper sizes, shape data, the .qdiagram round trip (unknown members kept,
// newer files refused, atomic save), clipboard fragments, and
// DrawingDocument's snapshot undo (one step per edit, merge keys, dirty
// follows the undo stack, recovery).

#include "diagram_fixture.h"

#include "drawing_document.h"
#include "drawing_io.h"
#include "painter.h"
#include "stencils.h"
#include "themes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>

using namespace DiagramTest;

class DiagramModelTest final : public QObject {
    Q_OBJECT
private slots:
    void idsNeverRepeat()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("decision", 300, 100);
        QVERIFY(!a.isEmpty() && !b.isEmpty() && a != b);
        // A hand-edited file that already uses the next number is skipped.
        f.page().shapes[0].id = QStringLiteral("s%1").arg(f.drawing.serial + 1);
        const QString c = f.shape("process", 100, 300);
        QVERIFY(c != f.page().shapes[0].id);
        QCOMPARE(f.page().shapes.size(), 3);
        QVERIFY(Ops::addShape(f.drawing, f.page(), s("noSuchStencil"), {}).isEmpty());
    }

    void catalogueCoversEveryStencilTheBriefNames()
    {
        const QStringList wanted{s("rectangle"), s("roundedRectangle"), s("ellipse"), s("diamond"), s("triangle"),
                                 s("text"), s("note"), s("process"), s("decision"), s("terminator"), s("data"),
                                 s("document"), s("predefinedProcess"), s("manualInput"), s("onPage"), s("offPage"),
                                 s("orgPerson"), s("orgTeam"), s("server"), s("desktop"), s("laptop"), s("router"),
                                 s("switch"), s("firewall"), s("cloud"), s("networkDatabase"), s("printer"),
                                 s("phone"), s("laneHorizontal"), s("laneVertical"), s("phase"), s("container"),
                                 s("umlClass"), s("umlActor"), s("umlUseCase"), s("arrowRight"), s("calloutRect")};
        for (const QString &id : wanted) {
            QVERIFY2(stencil(id) != nullptr, qPrintable(id));
        }
        QCOMPARE(categoryIds().size(), 7);
        for (const Stencil &st : stencils()) {
            QVERIFY2(categoryIds().contains(st.category), qPrintable(st.id));
            QVERIFY(!st.title.isEmpty());
            QVERIFY(st.size.width() > 0 && st.size.height() > 0);
            // Every stencil's outline is a closed, non-empty area.
            Shape shape;
            shape.type = st.id;
            shape.rect = QRectF(QPointF(0, 0), st.size);
            QVERIFY2(!outline(shape).isEmpty(), qPrintable(st.id));
            QVERIFY2(!stencilThumbnail(st.id, s("qinda"), 48).isNull(), qPrintable(st.id));
        }
    }

    void paperSizesAndData()
    {
        QCOMPARE(paperSize(s("letter")), QSizeF(816, 1056));
        QCOMPARE(paperSize(s("a4")), QSizeF(794, 1123));
        QCOMPARE(paperNameOf(QSizeF(1056, 816)), s("letter"));
        QCOMPARE(paperNameOf(QSizeF(1123, 794)), s("a4"));
        QCOMPARE(paperNameOf(QSizeF(500, 500)), s("custom"));
        QList<DataField> data;
        QVERIFY(setDataValue(data, s("Phone"), s("555-0100")));
        QVERIFY(setDataValue(data, s("phone"), s("555-0199"))); // keys are case-insensitive
        QVERIFY(!setDataValue(data, s("  "), s("x")));
        QCOMPARE(data.size(), 1);
        QCOMPARE(dataValue(data, s("PHONE")), s("555-0199"));
    }

    void fileRoundTripKeepsEverythingAndUnknownMembers()
    {
        Fixture f;
        const QString a = f.shape("process", 120, 100, "Sign up\nonline");
        const QString b = f.shape("decision", 360, 100, "Approved?");
        const QString c = f.connect(a, b, 1, 3);
        Ops::setData(f.page(), a, s("Owner"), s("Volunteer desk"));
        Ops::setText(f.page(), c, s("next"));
        f.page().shapes[0].rotation = 15;
        f.page().shapes[0].flipX = true;
        f.page().shapes[0].style.shadow = true;
        f.page().shapes[0].style.gradient = QColor(10, 20, 30);
        f.page().shapes[0].textStyle.bold = true;
        f.page().shapes[0].image = QByteArray("\x89PNG fake", 9);
        f.page().lineJumps = true;
        f.drawing.themeId = s("forest");
        // Members from a newer minor revision survive a load and a save.
        QJsonObject root = QJsonDocument::fromJson(drawingToJson(f.drawing)).object();
        root.insert(s("futureRoot"), 1);
        QJsonObject page = root.value(s("pages")).toArray().first().toObject();
        page.insert(s("futurePage"), s("kept"));
        QJsonArray shapes = page.value(s("shapes")).toArray();
        QJsonObject shape = shapes.first().toObject();
        shape.insert(s("futureShape"), QJsonObject{{s("x"), 1}});
        shapes[0] = shape;
        page.insert(s("shapes"), shapes);
        root.insert(s("pages"), QJsonArray{page});

        Drawing loaded;
        QString error;
        QVERIFY2(drawingFromJson(QJsonDocument(root).toJson(), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.pages.size(), 1);
        const Page &p = loaded.pages.first();
        QCOMPARE(p.shapes.size(), 2);
        QCOMPARE(p.shapes.first().text, s("Sign up\nonline"));
        QCOMPARE(p.shapes.first().rotation, 15.0);
        QVERIFY(p.shapes.first().flipX);
        QCOMPARE(p.shapes.first().image, QByteArray("\x89PNG fake", 9));
        QCOMPARE(dataValue(p.shapes.first().data, s("owner")), s("Volunteer desk"));
        QCOMPARE(p.connectors.first().from.port, 1);
        QCOMPARE(p.connectors.first().text, s("next"));
        QVERIFY(p.lineJumps);
        QCOMPARE(loaded.themeId, s("forest"));
        QCOMPARE(loaded.serial, f.drawing.serial);
        QCOMPARE(p.shapes.first().extra.value(s("futureShape")).toObject().value(s("x")).toInt(), 1);
        QCOMPARE(p.extra.value(s("futurePage")).toString(), s("kept"));
        QCOMPARE(loaded.extra.value(s("futureRoot")).toInt(), 1);
        // And back out: the unknown members are written again.
        const QJsonObject again = QJsonDocument::fromJson(drawingToJson(loaded)).object();
        QCOMPARE(again.value(s("futureRoot")).toInt(), 1);
        // The known content is identical to the original.
        loaded.extra = {};
        loaded.pages[0].extra = {};
        loaded.pages[0].shapes[0].extra = {};
        QVERIFY(loaded == f.drawing);
    }

    void newerAndForeignFilesAreRefused()
    {
        Drawing d;
        QString error;
        QVERIFY(!drawingFromJson("{\"format\":\"qindadiagram\",\"version\":2,\"pages\":[{}]}", &d, &error));
        QVERIFY(error.contains(s("newer")));
        QVERIFY(!drawingFromJson("{\"format\":\"other\",\"version\":1}", &d, &error));
        QVERIFY(!drawingFromJson("not json", &d, &error));
        QVERIFY(!drawingFromJson("{\"format\":\"qindadiagram\",\"version\":1,\"pages\":[]}", &d, &error));
    }

    void saveIsAtomicAndLoads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(s("flow.qdiagram"));
        Fixture f;
        f.shape("terminator", 100, 60, "Start");
        QString error;
        QVERIFY2(saveDrawing(path, f.drawing, &error), qPrintable(error));
        Drawing loaded;
        QVERIFY(loadDrawing(path, &loaded, &error));
        QVERIFY(loaded == f.drawing);
        // A save into a directory that does not exist fails and says so.
        QVERIFY(!saveDrawing(dir.filePath(s("missing/flow.qdiagram")), f.drawing, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!loadDrawing(dir.filePath(s("absent.qdiagram")), &loaded, &error));
    }

    void fragmentsCarryConnectorsBetweenCopiedShapes()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 300, 100);
        const QString c = f.shape("process", 500, 100);
        f.connect(a, b);
        f.connect(b, c);
        QVector<Shape> shapes;
        QVector<Connector> connectors;
        QVERIFY(parseFragment(fragmentOf(f.page(), {a, b}), &shapes, &connectors));
        QCOMPARE(shapes.size(), 2);
        QCOMPARE(connectors.size(), 1); // b -> c is not between copied shapes
        QVERIFY(!parseFragment("{\"format\":\"qindadiagram\"}", &shapes, &connectors));
    }

    void documentUndoIsOneStepPerEditAndMergesNudges()
    {
        DrawingDocument doc;
        QSignalSpy changed(&doc, &DrawingDocument::drawingChanged);
        QVERIFY(!doc.isDirty());
        QString id;
        QVERIFY(doc.edit(s("Add"), [&](Drawing &d) {
            id = Ops::addShape(d, d.pages.first(), s("process"), QPointF(100, 100));
            return !id.isEmpty();
        }));
        QVERIFY(doc.isDirty());
        QCOMPARE(doc.undoStack()->count(), 1);
        QCOMPARE(changed.count(), 1);
        // A no-op edit records nothing.
        QVERIFY(!doc.edit(s("Nothing"), [](Drawing &) { return true; }));
        QCOMPARE(doc.undoStack()->count(), 1);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(doc.edit(s("Nudge"), [&](Drawing &d) { return Ops::moveItems(d.pages.first(), {id}, QPointF(1, 0)); },
                             s("nudge")));
        }
        QCOMPARE(doc.undoStack()->count(), 2); // three nudges merged
        QCOMPARE(doc.drawing().pages.first().shape(id)->rect.center().x(), 103.0);
        doc.undoStack()->undo();
        QCOMPARE(doc.drawing().pages.first().shape(id)->rect.center().x(), 100.0);
        doc.undoStack()->undo();
        QVERIFY(doc.drawing().pages.first().shapes.isEmpty());
        QVERIFY(!doc.isDirty()); // back to the clean state
        doc.undoStack()->redo();
        QCOMPARE(doc.drawing().pages.first().shapes.size(), 1);
        QVERIFY(doc.isDirty());
        // Recovery restores exactly the snapshot and leaves it dirty.
        const QByteArray snapshot = doc.recoverySnapshot();
        DrawingDocument other;
        QVERIFY(other.loadRecovery(snapshot));
        QVERIFY(other.drawing() == doc.drawing());
        QVERIFY(other.isDirty());
        // The native format first, then the Visio drawings it opens (M65).
        QCOMPARE(other.mimeTypes().first(), s("application/x-qindadiagram"));
        QVERIFY(other.mimeTypes().contains(s("application/vnd.ms-visio.drawing.main+xml")));
    }

    void documentSavesAndLoads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(s("org.qdiagram"));
        DrawingDocument doc;
        QVERIFY(doc.edit(s("Add"), [](Drawing &d) {
            return !Ops::addShape(d, d.pages.first(), s("orgPerson"), QPointF(200, 80)).isEmpty();
        }));
        QString error;
        QVERIFY2(doc.save(path, &error), qPrintable(error));
        QVERIFY(!doc.isDirty());
        QCOMPARE(doc.canonicalPath(), path);
        DrawingDocument reopened;
        QSignalSpy reset(&reopened, &DrawingDocument::drawingReset);
        QVERIFY(reopened.load(path, &error));
        QCOMPARE(reset.count(), 1);
        QVERIFY(reopened.drawing() == doc.drawing());
        QCOMPARE(reopened.undoStack()->count(), 0);
    }

    void themesRestyleEverythingButKeepSizes()
    {
        Fixture f;
        const QString a = f.shape("process", 100, 100);
        const QString b = f.shape("process", 300, 100);
        const QString c = f.connect(a, b);
        f.page().connector(c)->textStyle.size = 14;
        f.page().shape(a)->style.lineWidth = 4;
        QVERIFY(applyTheme(f.drawing, s("sunset")));
        QCOMPARE(f.drawing.themeId, s("sunset"));
        QCOMPARE(f.page().shape(a)->style.fill, theme(s("sunset")).roles.first().fill);
        QCOMPARE(f.page().shape(a)->style.lineWidth, 4.0);
        QCOMPARE(f.page().connector(c)->line, theme(s("sunset")).connector);
        QCOMPARE(f.page().connector(c)->textStyle.size, 14.0);
        QVERIFY(!applyTheme(f.drawing, s("nope")));
        QCOMPARE(themes().size(), 6);
    }
};

DIAGRAM_TEST_MAIN(DiagramModelTest)

#include "tst_diagram_model.moc"
