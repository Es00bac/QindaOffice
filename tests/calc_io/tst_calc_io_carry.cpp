// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075, D-068 rule 2): the workbook parts QindaCalc does not model
// survive a save — copied from the source package with their relationships
// and wired back to the sheet and workbook parts that referenced them:
// pivot tables and their caches (marked refreshOnLoad so Excel recomputes
// them from the edited data), slicers and timelines with their caches and
// drawing anchors, a plain shape, query tables and their connection,
// external links, the data model and its custom XML. A pivot table follows
// a rename of its data sheet and rows inserted above it; one on a deleted
// sheet goes with the sheet. Every saved package is schema-valid and has
// no dangling relationship.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_fixtures.h"
#include "carry_inspect.h"

#include "structure.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;

class CalcIoCarryTest : public QObject {
    Q_OBJECT
private slots:
    void pivotTableTravelsWithItsCache();
    void slicersTimelinesAndShapesTravel();
    void pivotFollowsRenamesAndInsertedRows();
    void pivotOnDeletedSheetGoesWithIt();
    void connectionsLinksAndModelTravel();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    // Writes `book`, checks it is schema-valid, and reads it into `wb`.
    bool open(const ExcelWorkbook &book, const QString &name, Workbook *wb)
    {
        const QString source = path(name);
        QFile file(source);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(book.build());
        file.close();
        const QString schema = xlsxSchemaErrors(source);
        if (!schema.isEmpty()) {
            qWarning("fixture %s is not schema-valid: %s", qPrintable(name), qPrintable(schema));
            return false;
        }
        QString error;
        if (!IO::readXlsxFile(source, wb, &error)) {
            qWarning("read failed: %s", qPrintable(error));
            return false;
        }
        return true;
    }
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name)
    {
        QString error;
        if (!IO::writeXlsxFile(wb, path(name), &error)) {
            qWarning("write failed: %s", qPrintable(error));
            return nullptr;
        }
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("saved package is not schema-valid: %s", qPrintable(schema));
            return nullptr;
        }
        keepForInspection(path(name));
        auto package = Package::load(path(name), &error);
        if (package) {
            const QStringList broken = brokenWiring(*package);
            if (!broken.isEmpty()) {
                qWarning("broken wiring: %s", qPrintable(broken.join(QLatin1String("; "))));
                return nullptr;
            }
        }
        return package;
    }
};

namespace {

const QString kWorkbook = QStringLiteral("xl/workbook.xml");

QString pivotCacheIdPart(const Package &package, QString *cacheId = nullptr)
{
    const auto root = package.xml(kWorkbook);
    const XmlElement *caches = root ? root->child(sml(), u"pivotCaches") : nullptr;
    const XmlElement *cache = caches != nullptr ? caches->child(sml(), u"pivotCache") : nullptr;
    if (cache == nullptr) {
        return {};
    }
    if (cacheId != nullptr) {
        *cacheId = cache->attr(u"cacheId");
    }
    return partForId(package, kWorkbook, cache->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
}

} // namespace

void CalcIoCarryTest::pivotTableTravelsWithItsCache()
{
    Workbook wb;
    QVERIFY(open(pivotBook(false), QStringLiteral("pivot.xlsx"), &wb));
    // An edit to the pivot's source data: the cache must refresh on open.
    QVERIFY(wb.setCellInput(0, {1, 1}, QStringLiteral("15")));
    const auto package = save(wb, QStringLiteral("pivot-saved.xlsx"));
    QVERIFY(package);
    const QString pivotSheet = savedSheetPart(*package, QStringLiteral("Pivot"));
    const QStringList pivots = partsByType(*package, pivotSheet, officeRel("pivotTable"));
    QCOMPARE(pivots.size(), 1);
    const auto pivotXml = package->xml(pivots.first());
    QVERIFY(pivotXml);
    QCOMPARE(pivotXml->attr(u"name"), QStringLiteral("PivotTable1"));
    QCOMPARE(pivotXml->child(sml(), u"location")->attr(u"ref"), QStringLiteral("A3:B6"));
    // The pivot table's cache and the workbook's <pivotCache> are the SAME
    // part, with the same cacheId the pivot table names.
    const QStringList caches = partsByType(*package, pivots.first(), officeRel("pivotCacheDefinition"));
    QCOMPARE(caches.size(), 1);
    QString cacheId;
    QCOMPARE(pivotCacheIdPart(*package, &cacheId), caches.first());
    QCOMPARE(cacheId, pivotXml->attr(u"cacheId"));
    QCOMPARE(partsByType(*package, kWorkbook, officeRel("pivotCacheDefinition")), caches);
    const auto cacheXml = package->xml(caches.first());
    QVERIFY(cacheXml);
    QCOMPARE(cacheXml->attr(u"refreshOnLoad"), QStringLiteral("1"));
    const XmlElement *source = cacheXml->child(sml(), u"cacheSource")->child(sml(), u"worksheetSource");
    QCOMPARE(source->attr(u"sheet"), QStringLiteral("Data"));
    QCOMPARE(source->attr(u"ref"), QStringLiteral("A1:B4"));
    // The records travel with their definition.
    const QStringList records = partsByType(*package, caches.first(), officeRel("pivotCacheRecords"));
    QCOMPARE(records.size(), 1);
    QVERIFY(package->data(records.first()).contains("<n v=\"30\"/>"));
    QCOMPARE(package->contentType(pivots.first()), smlType("pivotTable+xml"));
    QCOMPARE(package->contentType(caches.first()), smlType("pivotCacheDefinition+xml"));
    QCOMPARE(package->contentType(records.first()), smlType("pivotCacheRecords+xml"));
}

void CalcIoCarryTest::slicersTimelinesAndShapesTravel()
{
    Workbook wb;
    QVERIFY(open(pivotBook(true), QStringLiteral("slicers.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("slicers-saved.xlsx"));
    QVERIFY(package);
    const QString pivotSheet = savedSheetPart(*package, QStringLiteral("Pivot"));
    const auto sheetXml = package->xml(pivotSheet);
    QVERIFY(sheetXml);
    // The sheet's extLst names the slicer and the timeline through ids that
    // resolve to the carried parts.
    const XmlElement *slicer = findLocal(*sheetXml, u"slicer");
    QVERIFY2(slicer != nullptr, "no x14:slicer in the sheet's extLst");
    // Excel finds an extension by its uri: the GUIDs stay exactly as written.
    QVERIFY(package->data(pivotSheet).contains("uri=\"{A8765BA9-456A-4dab-B4F3-ACF838C121DE}\""));
    QVERIFY(package->data(pivotSheet).contains("uri=\"{7E03D99C-DC04-49d9-9315-930204A7B6E9}\""));
    QVERIFY(package->data(kWorkbook).contains("uri=\"{BBE1A952-AA13-448e-AADC-164F8A28A991}\""));
    const QString slicerPart = partForId(*package, pivotSheet,
                                         slicer->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QVERIFY(package->data(slicerPart).contains("cache=\"Slicer_Region\""));
    const XmlElement *timeline = findLocal(*sheetXml, u"timelineRef");
    QVERIFY2(timeline != nullptr, "no x15:timelineRef in the sheet's extLst");
    QVERIFY(package->data(partForId(*package, pivotSheet,
                                    timeline->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id")))
                .contains("NativeTimeline_Date"));
    // The workbook's extLst names both caches.
    const auto workbookXml = package->xml(kWorkbook);
    const XmlElement *slicerCache = findLocal(*workbookXml, u"slicerCache");
    QVERIFY(slicerCache != nullptr);
    const QString slicerCachePart = partForId(*package, kWorkbook,
                                              slicerCache->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QVERIFY(package->data(slicerCachePart).contains("name=\"Slicer_Region\""));
    // tabId is the sheetId of the pivot table's sheet in the SAVED workbook.
    const QString pivotSheetId = savedSheetIds(*package).value(QStringLiteral("Pivot"));
    QVERIFY(package->data(slicerCachePart).contains("tabId=\"" + pivotSheetId.toUtf8() + "\""));
    const XmlElement *timelineCache = findLocal(*workbookXml, u"timelineCacheRef");
    QVERIFY(timelineCache != nullptr);
    QVERIFY(package->data(partForId(*package, kWorkbook,
                                    timelineCache->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id")))
                .contains("tabId=\"" + pivotSheetId.toUtf8() + "\""));
    // The slicer, the timeline and the text box keep their drawing anchors.
    const QStringList drawings = partsByType(*package, pivotSheet, officeRel("drawing"));
    QCOMPARE(drawings.size(), 1);
    const QByteArray drawing = package->data(drawings.first());
    QVERIFY(drawing.contains("name=\"Region\""));
    QVERIFY(drawing.contains("name=\"NativeTimeline_Date\""));
    QVERIFY(drawing.contains("Totals are refreshed on open"));
}

void CalcIoCarryTest::pivotFollowsRenamesAndInsertedRows()
{
    Workbook wb;
    QVERIFY(open(pivotBook(true), QStringLiteral("moves.xlsx"), &wb));
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Sales Data")));
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 1, 0, 2});
    const auto package = save(wb, QStringLiteral("moves-saved.xlsx"));
    QVERIFY(package);
    const QString cachePart = pivotCacheIdPart(*package);
    QVERIFY(!cachePart.isEmpty());
    const auto cacheXml = package->xml(cachePart);
    const XmlElement *source = cacheXml->child(sml(), u"cacheSource")->child(sml(), u"worksheetSource");
    QCOMPARE(source->attr(u"sheet"), QStringLiteral("Sales Data"));
    const QString pivotSheet = savedSheetPart(*package, QStringLiteral("Pivot"));
    const QStringList pivots = partsByType(*package, pivotSheet, officeRel("pivotTable"));
    QCOMPARE(pivots.size(), 1);
    QCOMPARE(package->xml(pivots.first())->child(sml(), u"location")->attr(u"ref"), QStringLiteral("A5:B8"));
    // Every carried anchor is two rows lower: the text box (rows 8-10, 0-based)
    // and the timeline (rows 13-20) — so no anchor still says row 8 or 20.
    const QByteArray drawing = package->data(partsByType(*package, pivotSheet, officeRel("drawing")).value(0));
    QVERIFY2(drawing.contains("<xdr:row>22</xdr:row>") && drawing.contains("<xdr:row>12</xdr:row>"),
             drawing.constData());
    QVERIFY2(!drawing.contains("<xdr:row>20</xdr:row>") && !drawing.contains("<xdr:row>8</xdr:row>"),
             drawing.constData());
}

void CalcIoCarryTest::pivotOnDeletedSheetGoesWithIt()
{
    Workbook wb;
    QVERIFY(open(pivotBook(true), QStringLiteral("deleted.xlsx"), &wb));
    QVERIFY(removeSheet(wb, 1));
    const auto package = save(wb, QStringLiteral("deleted-saved.xlsx"));
    QVERIFY(package);
    for (const QString &part : package->partNames()) {
        QVERIFY2(!part.contains(QLatin1String("pivot")) && !part.contains(QLatin1String("slicer"))
                     && !part.contains(QLatin1String("timeline")),
                 qPrintable(part));
    }
    const auto workbookXml = package->xml(kWorkbook);
    QVERIFY(workbookXml->child(sml(), u"pivotCaches") == nullptr);
    QVERIFY(findLocal(*workbookXml, u"slicerCache") == nullptr);
}

void CalcIoCarryTest::connectionsLinksAndModelTravel()
{
    Workbook wb;
    QVERIFY(open(connectionBook(), QStringLiteral("connections.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("connections-saved.xlsx"));
    QVERIFY(package);
    // The connection the query table and the data model use.
    const QStringList connections = partsByType(*package, kWorkbook, officeRel("connections"));
    QCOMPARE(connections.size(), 1);
    QVERIFY(package->data(connections.first()).contains("Query - Inventory"));
    // The query table hangs from the TABLE part, which says it is one.
    const QString sheetPart = savedSheetPart(*package, QStringLiteral("Inventory"));
    const QStringList tables = partsByType(*package, sheetPart, officeRel("table"));
    QCOMPARE(tables.size(), 1);
    QCOMPARE(package->xml(tables.first())->attr(u"tableType"), QStringLiteral("queryTable"));
    const QStringList queries = partsByType(*package, tables.first(), officeRel("queryTable"));
    QCOMPARE(queries.size(), 1);
    QCOMPARE(package->xml(queries.first())->attr(u"connectionId"), QStringLiteral("1"));
    // The external link: its part, its <externalReference> (first, so the
    // formula's [1] still names it), and its external target.
    const auto workbookXml = package->xml(kWorkbook);
    const XmlElement *references = workbookXml->child(sml(), u"externalReferences");
    QVERIFY(references != nullptr);
    const QString linkPart = partForId(*package, kWorkbook,
                                       references->child(sml(), u"externalReference")
                                           ->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QVERIFY(package->data(linkPart).contains("sheetName val=\"Prices\""));
    bool external = false;
    for (const auto &rel : package->relationships(linkPart)) {
        external = external || (rel.external && rel.target == QLatin1String("Prices.xlsx"));
    }
    QVERIFY(external);
    const QHash<QString, SavedCell> cells = savedCells(*package, sheetPart);
    QCOMPARE(cells.value(QStringLiteral("D1")).formula, QStringLiteral("[1]Prices!A1*2"));
    // The data model and its mashup.
    const QStringList models = partsByType(*package, kWorkbook, officeRel("powerPivotData"));
    QCOMPARE(models.size(), 1);
    QCOMPARE(package->data(models.first()), QByteArrayLiteral("PKDATAMODEL-OPAQUE-BYTES"));
    const QStringList custom = partsByType(*package, kWorkbook, officeRel("customXml"));
    QCOMPARE(custom.size(), 1);
    QCOMPARE(partsByType(*package, custom.first(), officeRel("customXmlProps")).size(), 1);
    QVERIFY(findLocal(*workbookXml, u"dataModel") != nullptr);
}

QTEST_GUILESS_MAIN(CalcIoCarryTest)
#include "tst_calc_io_carry.moc"
