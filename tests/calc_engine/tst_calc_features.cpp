// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075) engine behaviour of the Excel features around the cells:
// hyperlinks follow inserted and removed rows and sheet renames (their own
// cells and the places they point at); outline groups form, collapse and
// expand the way Excel's +/- and level buttons do, and keep their levels
// through structural edits; protection hashes passwords as Excel does and
// locks cells by their format; the A1# spill reference follows its spill;
// and a sourced sheet's journal maps source positions to current ones.

#include "hyperlink.h"
#include "outline.h"
#include "protection.h"
#include "sheet_source.h"
#include "structure.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcFeaturesTest : public QObject {
    Q_OBJECT
private slots:
    void hyperlinksFollowStructuralEdits();
    void hyperlinkTargets();
    void outlineGroupsCollapseAndExpand();
    void outlineLevelsFollowStructuralEdits();
    void passwordHashMatchesExcel();
    void lockedCellsUnderProtection();
    void spillReferenceFollowsTheSpill();
    void journalMapsSourcePositions();
};

void CalcFeaturesTest::hyperlinksFollowStructuralEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Links"));
    wb.addSheet(QStringLiteral("Q1 Sales"));
    Hyperlink external{{{2, 1}, {2, 1}}, QStringLiteral("https://example.com/"), {}, {}, {}};
    Hyperlink internal{{{0, 0}, {0, 0}}, {}, QStringLiteral("'Q1 Sales'!C5"), {}, {}};
    wb.sheetAt(0).setHyperlinks({external, internal});
    // Rows inserted above the external link move it; the internal link at
    // A1 stays (it is above the insert).
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 1, 2});
    QVERIFY(wb.sheetAt(0).hyperlinkAt({4, 1}) != nullptr);
    QCOMPARE(wb.sheetAt(0).hyperlinkAt({4, 1})->address, QStringLiteral("https://example.com/"));
    QVERIFY(wb.sheetAt(0).hyperlinkAt({2, 1}) == nullptr);
    // A row inserted on the target sheet moves the place the link names.
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 1, 0, 1});
    QCOMPARE(wb.sheetAt(0).hyperlinkAt({0, 0})->location, QStringLiteral("'Q1 Sales'!C6"));
    // Renaming the target sheet re-points it.
    QVERIFY(renameSheet(wb, 1, QStringLiteral("Sales")));
    QCOMPARE(wb.sheetAt(0).hyperlinkAt({0, 0})->location, QStringLiteral("Sales!C6"));
    // Removing the link's own row removes the link.
    applyStructureEdit(wb, StructureEdit{StructureOp::RemoveRows, 0, 4, 1});
    QCOMPARE(wb.sheetAt(0).hyperlinks().size(), 1);
}

void CalcFeaturesTest::hyperlinkTargets()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    wb.addSheet(QStringLiteral("Sheet 2"));
    Hyperlink link;
    setHyperlinkTarget(&link, QStringLiteral("www.example.com"));
    QCOMPARE(link.address, QStringLiteral("http://www.example.com"));
    setHyperlinkTarget(&link, QStringLiteral("sales@example.com"));
    QCOMPARE(link.address, QStringLiteral("mailto:sales@example.com"));
    setHyperlinkTarget(&link, QStringLiteral("#'Sheet 2'!B3"));
    QVERIFY(link.address.isEmpty());
    QCOMPARE(link.location, QStringLiteral("'Sheet 2'!B3"));
    QCOMPARE(hyperlinkTarget(link), QStringLiteral("#'Sheet 2'!B3"));
    const auto place = resolveLinkLocation(wb, 0, link.location);
    QVERIFY(place.has_value());
    QCOMPARE(place->sheet, 1);
    QCOMPARE(place->range.topLeft, (CellAddr{2, 1}));
    QVERIFY(!resolveLinkLocation(wb, 0, QStringLiteral("Nowhere!A1")).has_value());
    RangeRef named;
    named.sheet = QStringLiteral("Data");
    named.range = {{4, 0}, {6, 1}};
    wb.defineName(QStringLiteral("Totals"), named);
    QCOMPARE(resolveLinkLocation(wb, 1, QStringLiteral("Totals"))->sheet, 0);
    // HYPERLINK(): the link is the evaluated first argument.
    wb.setCellValue(0, {0, 0}, QStringLiteral("orders"));
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=HYPERLINK(\"https://example.com/\"&A1,\"Open\")")));
    QCOMPARE(toText(wb.cellValue(0, 0, 1)), QStringLiteral("Open"));
    QCOMPARE(hyperlinkFormulaTarget(wb, 0, {0, 1}), QStringLiteral("https://example.com/orders"));
    QVERIFY(hyperlinkFormulaTarget(wb, 0, {0, 0}).isEmpty());
}

void CalcFeaturesTest::outlineGroupsCollapseAndExpand()
{
    SheetOutline outline;
    QVERIFY(groupLines(outline, OutlineAxis::Rows, 1, 6)); // rows 2-7, level 1
    QVERIFY(groupLines(outline, OutlineAxis::Rows, 2, 3)); // rows 3-4, level 2
    QCOMPARE(outline.maxLevel(OutlineAxis::Rows), quint8(2));
    const QList<OutlineGroup> groups = outlineGroups(outline, OutlineAxis::Rows);
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0), (OutlineGroup{1, 6, 1, 7, false}));
    QCOMPARE(groups.at(1), (OutlineGroup{2, 3, 2, 4, false}));
    // Hide Detail of the inner group: its rows hidden, its summary marked.
    setGroupCollapsed(outline, OutlineAxis::Rows, groups.at(1), true);
    QVERIFY(outline.isHidden(OutlineAxis::Rows, 2) && outline.isHidden(OutlineAxis::Rows, 3));
    QVERIFY(outline.line(OutlineAxis::Rows, 4).collapsed);
    // Collapse and re-open the outer group: the inner one stays collapsed.
    setGroupCollapsed(outline, OutlineAxis::Rows, groups.at(0), true);
    QVERIFY(outline.isHidden(OutlineAxis::Rows, 6));
    setGroupCollapsed(outline, OutlineAxis::Rows, outlineGroups(outline, OutlineAxis::Rows).at(0), false);
    QVERIFY(!outline.isHidden(OutlineAxis::Rows, 1) && !outline.isHidden(OutlineAxis::Rows, 6));
    QVERIFY(outline.isHidden(OutlineAxis::Rows, 2));
    QCOMPARE(groupAt(outline, OutlineAxis::Rows, 3)->level, quint8(2));
    QCOMPARE(groupAt(outline, OutlineAxis::Rows, 7)->level, quint8(1)); // the outer summary row
    // Level buttons: 1 shows only ungrouped rows, 3 shows everything.
    showOutlineLevel(outline, OutlineAxis::Rows, 1);
    QVERIFY(outline.isHidden(OutlineAxis::Rows, 1) && outline.isHidden(OutlineAxis::Rows, 5));
    QVERIFY(outline.line(OutlineAxis::Rows, 7).collapsed);
    showOutlineLevel(outline, OutlineAxis::Rows, 3);
    for (qint32 row = 1; row <= 6; ++row) {
        QVERIFY(!outline.isHidden(OutlineAxis::Rows, row));
    }
    QVERIFY(!outline.line(OutlineAxis::Rows, 7).collapsed);
    // Ungroup peels one level; summary marks of vanished groups go.
    QVERIFY(ungroupLines(outline, OutlineAxis::Rows, 2, 3));
    QCOMPARE(outline.maxLevel(OutlineAxis::Rows), quint8(1));
    // Summary above the detail (summaryBelow="0"): the row before.
    outline.summaryBelow = false;
    QCOMPARE(outlineGroups(outline, OutlineAxis::Rows).first().summary, 0);
    QVERIFY(!groupLines(outline, OutlineAxis::Columns, 5, 4)); // an empty span changes nothing
}

void CalcFeaturesTest::outlineLevelsFollowStructuralEdits()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Budget"));
    SheetOutline outline;
    groupLines(outline, OutlineAxis::Rows, 2, 5); // rows 3-6
    sheet.setOutline(outline);
    // Rows inserted inside the group join it; at its edge they do not.
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 4, 2});
    QCOMPARE(wb.sheetAt(0).outline().line(OutlineAxis::Rows, 4).level, quint8(1));
    QCOMPARE(wb.sheetAt(0).outline().line(OutlineAxis::Rows, 7).level, quint8(1));
    QCOMPARE(outlineGroups(wb.sheetAt(0).outline(), OutlineAxis::Rows).first(), (OutlineGroup{2, 7, 1, 8, false}));
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 2, 1});
    QCOMPARE(wb.sheetAt(0).outline().line(OutlineAxis::Rows, 2).level, quint8(0));
    QCOMPARE(outlineGroups(wb.sheetAt(0).outline(), OutlineAxis::Rows).first().first, 3);
    // Removed rows take their levels with them.
    applyStructureEdit(wb, StructureEdit{StructureOp::RemoveRows, 0, 3, 3});
    QCOMPARE(outlineGroups(wb.sheetAt(0).outline(), OutlineAxis::Rows).first(), (OutlineGroup{3, 5, 1, 6, false}));
    // Columns move on their own axis only.
    SheetOutline columns = wb.sheetAt(0).outline();
    groupLines(columns, OutlineAxis::Columns, 1, 2);
    wb.sheetAt(0).setOutline(columns);
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertColumns, 0, 0, 1});
    QCOMPARE(outlineGroups(wb.sheetAt(0).outline(), OutlineAxis::Columns).first().first, 2);
    QCOMPARE(outlineGroups(wb.sheetAt(0).outline(), OutlineAxis::Rows).first().first, 3);
}

void CalcFeaturesTest::passwordHashMatchesExcel()
{
    // Vectors computed independently (Python hashlib, the documented
    // procedure); "password" → 83AF is Excel's own legacy verifier.
    QByteArray salt;
    for (int i = 0; i < 16; ++i) {
        salt.append(char(i));
    }
    const PasswordHash hash = excelPasswordHash(QStringLiteral("secret"), salt);
    QCOMPARE(hash.algorithmName, QStringLiteral("SHA-512"));
    QCOMPARE(hash.saltValue, QStringLiteral("AAECAwQFBgcICQoLDA0ODw=="));
    QCOMPARE(hash.spinCount, 100000);
    QCOMPARE(hash.hashValue, QStringLiteral("M5SOVnbQG4SHyBnRVAYzAx8mPtxyyzMuWxcMv7tkyFO3MBXX9OJjklwPglNHdoHVkKPm4MPfUblqHmAsXfF5HA=="));
    QCOMPARE(excelPasswordHash(QStringLiteral("Pässwörd"), salt).hashValue,
             QStringLiteral("5jLCdMFftKPoqAQ+kkW9gRb3w63rSt+HtunE0UCYQUDpUtwUeP+uKzYnZZx9Na9JPpAhHN84y+73k8lwv6waQQ=="));
    QCOMPARE(legacyPasswordHash(QStringLiteral("password")), QStringLiteral("83AF"));
    QCOMPARE(legacyPasswordHash(QStringLiteral("secret")), QStringLiteral("DAA7"));
    SheetProtection protection;
    protection.enabled = true;
    QVERIFY(passwordMatches(protection, QString())); // no password set
    setPassword(&protection, QStringLiteral("secret"));
    QCOMPARE(protection.algorithmName, QStringLiteral("SHA-512"));
    QCOMPARE(protection.spinCount, 100000);
    QVERIFY(passwordMatches(protection, QStringLiteral("secret")));
    QVERIFY(!passwordMatches(protection, QStringLiteral("Secret")));
    SheetProtection legacy;
    legacy.legacyHash = QStringLiteral("83af");
    QVERIFY(passwordMatches(legacy, QStringLiteral("password")));
    QVERIFY(!passwordMatches(legacy, QStringLiteral("passw0rd")));
    WorkbookProtection book;
    setPassword(&book, QStringLiteral("Pässwörd"));
    QVERIFY(passwordMatches(book, QStringLiteral("Pässwörd")));
    setPassword(&book, QString());
    QVERIFY(!book.hasPassword());
}

void CalcFeaturesTest::lockedCellsUnderProtection()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Rates"));
    CellStyle open;
    open.locked = false;
    CellStyle secret;
    secret.hiddenFormula = true;
    wb.sheetAt(0).setStyleIdAt({0, 0}, wb.addStyle(open));
    wb.sheetAt(0).setStyleIdAt({0, 1}, wb.addStyle(secret));
    QVERIFY(!wb.isCellLocked(0, {0, 2})); // not protected: nothing is locked
    SheetProtection protection;
    protection.enabled = true;
    wb.sheetAt(0).setProtection(protection);
    QVERIFY(wb.isCellLocked(0, {0, 2})); // the default format is locked
    QVERIFY(!wb.isCellLocked(0, {0, 0}));
    QVERIFY(wb.isCellLocked(0, {0, 1}));
    QVERIFY(wb.isFormulaHidden(0, {0, 1}));
    QVERIFY(!wb.isFormulaHidden(0, {0, 0}));
}

void CalcFeaturesTest::spillReferenceFollowsTheSpill()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    wb.setCellValue(0, {0, 0}, 3.0);
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(A1)")));
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=SUM(B1#)")));
    QCOMPARE(wb.cellValue(0, 0, 3), Value(6.0));
    QCOMPARE(wb.sheetAt(0).cellAt({0, 3})->formula, QStringLiteral("SUM(B1#)"));
    QCOMPARE(nodeText(wb.sheetAt(0).cellAt({0, 3})->ast), QStringLiteral("SUM(B1#)"));
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("4")));
    QCOMPARE(wb.cellValue(0, 0, 3), Value(10.0));
    // A cell that spills nothing has no spill range.
    QVERIFY(wb.setCellInput(0, {0, 4}, QStringLiteral("=SUM(A1#)")));
    QCOMPARE(toText(wb.cellValue(0, 0, 4)), QStringLiteral("#REF!"));
    QCOMPARE(wb.spillAnchorOf(0, {2, 1}), (CellAddr{0, 1}));
    QCOMPARE(wb.spillAt(0, {0, 1})->extent, (CellRange{{0, 1}, {3, 1}}));
}

void CalcFeaturesTest::journalMapsSourcePositions()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Pivot"));
    sheet.setSource({QStringLiteral("xl/worksheets/sheet2.xml"), QStringLiteral("Sheet2"), {}});
    Sheet &fresh = wb.addSheet(QStringLiteral("New"));
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 0, 2});
    applyStructureEdit(wb, StructureEdit{StructureOp::RemoveColumns, 0, 1, 1});
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 1, 0, 5});
    Q_UNUSED(fresh);
    QCOMPARE(wb.sheetAt(0).source().steps.size(), 2);
    QVERIFY(wb.sheetAt(1).source().steps.isEmpty()); // no source: nothing to map
    const QList<SourceStep> &steps = wb.sheetAt(0).source().steps;
    QCOMPARE(mapSourceCell(steps, {2, 0}), std::optional<CellAddr>(CellAddr{4, 0}));
    QVERIFY(!mapSourceCell(steps, {2, 1}).has_value()); // its column was removed
    QCOMPARE(mapSourceRange(steps, {{2, 0}, {5, 3}}), std::optional<CellRange>(CellRange{{4, 0}, {7, 2}}));
    QCOMPARE(mapSourceLine(steps, true, 0), std::optional<qint32>(2));
}

QTEST_GUILESS_MAIN(CalcFeaturesTest)
#include "tst_calc_features.moc"
