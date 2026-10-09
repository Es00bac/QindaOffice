// SPDX-License-Identifier: GPL-3.0-or-later
// Word's Table Design and Table Layout in QindaWrite through the real
// window (M70, D-079): shading and borders on the selected cells and on
// the whole table with the ribbon's pen, select / delete / align / AutoFit
// / distribute / header rows / sort / convert, the Table Properties dialog
// — each command enabled only in a table and undone in one step — and the
// saved .docx carrying Word's markup. QQO_GRAB_DIR keeps the grabs: a
// shaded, bordered table and each dialog at 1280 and 520 px.

#include "write_test_support.h"

#include "docx_borders.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_table_format.h"
#include "qindawrite/src/table_layout.h"
#include "qindawrite/src/write_borders.h"
#include "support/quick_dump.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QKeyEvent>
#include <QTextTable>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
using QindaOffice::Quick::ActionEntry;
namespace Docx = QindaOffice::Docx;

namespace {

QTextTable *tableIn(QTextDocument *doc)
{
    for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(frame)) {
            return t;
        }
    }
    return nullptr;
}

void fill(QTextTable *table, const QStringList &rows)
{
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList cells = rows.at(r).split(QLatin1Char('|'));
        for (int c = 0; c < cells.size(); ++c) {
            table->cellAt(r, c).firstCursorPosition().insertText(cells.at(c));
        }
    }
}

QString cellText(QTextTable *table, int r, int c)
{
    QTextCursor cell = table->cellAt(r, c).firstCursorPosition();
    cell.setPosition(table->cellAt(r, c).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    return cell.selectedText();
}

QObject *openDialog(Scene &scene, const char *action, const char *name)
{
    ActionEntry *entry = scene.host.action(QString::fromLatin1(action));
    if (entry == nullptr || !entry->isEnabled()) {
        return nullptr;
    }
    entry->trigger();
    QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
    if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
        return nullptr;
    }
    QTest::qWait(150);
    return dialog;
}

} // namespace

class WriteTablesTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected"))); }
    void designCommandsShadeAndBorderCells();
    void layoutCommandsAndUndo();
    void propertiesSortAndConvert();
    void dialogsAtBothWidths();
    void headerRowsRepeatAndHtmlKeepsTheLook();
};

void WriteTablesTest::designCommandsShadeAndBorderCells()
{
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    auto *borders = scene.host.bordersController();
    ActionEntry *insideV = scene.host.action(QStringLiteral("writeBordersInsideVAction"));
    QVERIFY(insideV != nullptr && !insideV->isEnabled()); // between cells only
    QTextCursor(scene.doc->textDocument()).insertText(QStringLiteral("Prices"));
    scene.host.insertTable(3, 3);
    QTextTable *table = tableIn(scene.doc->textDocument());
    QVERIFY(table != nullptr);
    fill(table, {QStringLiteral("Item|Qty|Price"), QStringLiteral("Paper|2|4.50"), QStringLiteral("Ink|1|19.00")});
    QVERIFY(insideV->isEnabled());

    // The header row: shaded, a thick red line under it.
    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectRowAction")));
    QCOMPARE(Docx::selectedCells(scene.control()->textCursor()), (Docx::CellRange{0, 0, 1, 3}));
    // One step: Undo takes the whole row's shading back, Redo returns it
    // (QTextDocument counts edit commands, not steps, so the state is checked).
    borders->applyShading(QColor(0x1F, 0x4E, 0x79));
    QCOMPARE(table->cellAt(0, 2).format().toTableCellFormat().background().color(), QColor(0x1F, 0x4E, 0x79));
    scene.control()->undo();
    for (int c = 0; c < 3; ++c) {
        QVERIFY(!table->cellAt(0, c).format().hasProperty(QTextFormat::BackgroundBrush));
    }
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("Item"));
    scene.control()->redo();
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().background().color(), QColor(0x1F, 0x4E, 0x79));
    // Undo and Redo move the caret: select the row again.
    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectRowAction")));
    borders->setPenStyleIndex(5); // double
    borders->setPenSizeIndex(4);  // 1½ pt
    borders->setPenColor(QColor(0xC0, 0x00, 0x00));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersBottomAction")));
    QCOMPARE(table->cellAt(0, 2).format().toTableCellFormat().bottomBorderStyle(), QTextFrameFormat::BorderStyle_Double);
    QCOMPARE(table->cellAt(1, 0).format().toTableCellFormat().topBorderBrush().color(), QColor(0xC0, 0x00, 0x00));
    // Pressed again, the edge the selection draws is taken away (Word's toggle) …
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersBottomAction")));
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().bottomBorder(), 0.0);
    // … and Undo brings it back in one step.
    scene.control()->undo();
    QCOMPARE(table->cellAt(0, 0).format().toTableCellFormat().bottomBorderStyle(), QTextFrameFormat::BorderStyle_Double);

    // The whole table: a dashed outside border becomes the table's own.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectTableAction")));
    borders->setPenStyleIndex(2); // dashed
    borders->setPenSizeIndex(2);
    borders->setPenColor(QColor(0x2F, 0x54, 0x96));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersOutsideAction")));
    const Docx::WordBorders level = Docx::parseBordersSpec(table->format().stringProperty(Docx::kTableBorders));
    QCOMPARE(level.value(QStringLiteral("left")).style, QStringLiteral("dashed"));
    scene.control()->setTextCursor(table->cellAt(2, 1).firstCursorPosition());
    QTest::qWait(150);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_table_1280"));

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("prices.docx"));
    QString error;
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    const auto package = QindaOffice::Ooxml::Package::load(path);
    QVERIFY(package);
    const QByteArray xml = package->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"1F4E79\"/>"));
    QVERIFY(xml.contains("<w:bottom w:val=\"double\" w:sz=\"12\" w:space=\"0\" w:color=\"C00000\"/>"));
    QVERIFY(xml.contains("<w:left w:val=\"dashed\" w:sz=\"6\" w:space=\"0\" w:color=\"2F5496\"/>"));
    // No run took the cell's shading (typing in a shaded cell).
    QVERIFY(!xml.contains("<w:rPr><w:shd"));
    QTextDocument back;
    Docx::WordParts parts;
    QVERIFY2(Docx::readDocx(path, &back, &parts, &error), qPrintable(error));
    QTextTable *again = tableIn(&back);
    QCOMPARE(again->cellAt(0, 1).format().toTableCellFormat().background().color(), QColor(0x1F, 0x4E, 0x79));
}

void WriteTablesTest::layoutCommandsAndUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    ActionEntry *properties = scene.host.action(QStringLiteral("writeTablePropertiesAction"));
    ActionEntry *textToTable = scene.host.action(QStringLiteral("writeTextToTableAction"));
    QVERIFY(!properties->isEnabled());
    QVERIFY(textToTable->isEnabled());
    scene.host.insertTable(2, 3);
    QTextTable *table = tableIn(scene.doc->textDocument());
    fill(table, {QStringLiteral("a|bb|ccc"), QStringLiteral("d|e|f")});
    scene.control()->setTextCursor(table->cellAt(1, 1).firstCursorPosition());
    QVERIFY(properties->isEnabled());
    QVERIFY(!textToTable->isEnabled());
    // Tab in the last cell adds a row like the one above it, grid lines and all.
    scene.control()->setTextCursor(table->cellAt(1, 2).lastCursorPosition());
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
    QVERIFY(scene.control()->processEvent(&tab));
    QCOMPARE(table->rows(), 3);
    QVERIFY(table->cellAt(2, 0).format().toTableCellFormat().bottomBorder() > 0);
    QCOMPARE(table->cellAt(2, 1).format().toTableCellFormat().leftPadding(), table->cellAt(1, 1).format().toTableCellFormat().leftPadding());
    scene.control()->undo(); // the row in one step
    QCOMPARE(table->rows(), 2);
    scene.control()->setTextCursor(table->cellAt(1, 1).firstCursorPosition());

    // Align Bottom Right: the cell's vertical alignment and its paragraph.
    const auto valignBefore = table->cellAt(1, 1).format().toTableCellFormat().verticalAlignment();
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableAlignBottomRightAction")));
    QCOMPARE(table->cellAt(1, 1).format().toTableCellFormat().verticalAlignment(), QTextCharFormat::AlignBottom);
    QCOMPARE(table->cellAt(1, 1).firstCursorPosition().blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignRight);
    scene.control()->undo(); // both in one step
    QCOMPARE(table->cellAt(1, 1).format().toTableCellFormat().verticalAlignment(), valignBefore);
    QCOMPARE(table->cellAt(1, 1).firstCursorPosition().blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QCOMPARE(cellText(table, 1, 1), QStringLiteral("e"));

    // AutoFit window, then fixed; distribute; repeat the header row.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableAutoFitWindowAction")));
    QCOMPARE(table->format().width().type(), QTextLength::PercentageLength);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableFixedWidthAction")));
    QCOMPARE(table->format().columnWidthConstraints().at(0).type(), QTextLength::FixedLength);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableDistributeRowsAction")));
    QVERIFY(Docx::rowProperties(table, 0).height > 0);
    ActionEntry *header = scene.host.action(QStringLiteral("writeTableRepeatHeaderAction"));
    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    QVERIFY(!header->isChecked());
    header->trigger();
    QCOMPARE(table->format().headerRowCount(), 1);
    QVERIFY(header->isChecked());

    // Select Column, then Delete Table (one step back).
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectColumnAction")));
    QCOMPARE(Docx::selectedCells(scene.control()->textCursor()), (Docx::CellRange{0, 0, 2, 1}));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableDeleteTableAction")));
    QVERIFY(tableIn(scene.doc->textDocument()) == nullptr);
    QVERIFY(!properties->isEnabled());
    scene.control()->undo();
    QVERIFY(tableIn(scene.doc->textDocument()) != nullptr);
}

void WriteTablesTest::propertiesSortAndConvert()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    // Convert Text to Table: tab-separated lines become rows.
    QTextCursor typing(scene.doc->textDocument());
    typing.insertText(QStringLiteral("Name\tAge\nCleo\t41\nAbe\t7\nBea\t30\nEnd"));
    QTextCursor all(scene.doc->textDocument());
    all.setPosition(scene.doc->textDocument()->toPlainText().indexOf(QStringLiteral("\nEnd")), QTextCursor::KeepAnchor);
    scene.control()->setTextCursor(all);
    TableLayoutController *layout = scene.host.tableLayoutController();
    QCOMPARE(layout->textToTableInfo(QStringLiteral("tab")).value(QStringLiteral("columns")).toInt(), 2);
    QVERIFY(layout->applyTextToTable({{QStringLiteral("columns"), 2}, {QStringLiteral("separator"), QStringLiteral("tab")}}));
    QTextTable *table = tableIn(scene.doc->textDocument());
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 4);
    QCOMPARE(cellText(table, 2, 0), QStringLiteral("Abe"));
    // No empty paragraph left between the table and what followed it.
    QTextCursor after(scene.doc->textDocument());
    after.setPosition(table->lastPosition() + 1);
    QCOMPARE(after.block().text(), QStringLiteral("End"));
    // One step back is the text again; Redo the table.
    scene.control()->undo();
    QVERIFY(tableIn(scene.doc->textDocument()) == nullptr);
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Cleo\t41")));
    scene.control()->redo();
    table = tableIn(scene.doc->textDocument());
    QVERIFY(table != nullptr);
    QCOMPARE(cellText(table, 3, 1), QStringLiteral("30"));

    // Sort by Age as numbers, descending, the header row staying put.
    scene.control()->setTextCursor(table->cellAt(1, 0).firstCursorPosition()); // Redo moved the caret
    const QVariantMap info = layout->sortInfo();
    QCOMPARE(info.value(QStringLiteral("headings")).toStringList().value(1), QStringLiteral("Age"));
    const QString problem = layout->applySort({{QStringLiteral("column"), 1}, {QStringLiteral("type"), QStringLiteral("number")},
                                               {QStringLiteral("descending"), true}, {QStringLiteral("header"), true}});
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("Name"));
    QCOMPARE(cellText(table, 1, 0), QStringLiteral("Cleo"));
    QCOMPARE(cellText(table, 3, 0), QStringLiteral("Abe"));
    scene.control()->undo(); // one step
    QCOMPARE(cellText(table, 1, 0), QStringLiteral("Cleo"));
    QCOMPARE(cellText(table, 2, 0), QStringLiteral("Abe"));

    // Table Properties: centred, 60 % wide, the first row 0.5 in exactly.
    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    QVariantMap answer = layout->propertiesInfo();
    QVariantMap t = answer.value(QStringLiteral("table")).toMap();
    t.insert(QStringLiteral("widthType"), QStringLiteral("pct"));
    t.insert(QStringLiteral("width"), 60);
    t.insert(QStringLiteral("alignment"), QStringLiteral("center"));
    answer.insert(QStringLiteral("table"), t);
    answer.insert(QStringLiteral("row"), QVariantMap{{QStringLiteral("height"), 720}, {QStringLiteral("exact"), true},
                                                     {QStringLiteral("cantSplit"), true}, {QStringLiteral("header"), true}});
    answer.insert(QStringLiteral("changed"), QStringList{QStringLiteral("table"), QStringLiteral("row")});
    QVERIFY(layout->applyProperties(answer));
    QCOMPARE(Docx::tableProperties(table).width, (Docx::WordWidth{QStringLiteral("pct"), 3000}));
    QCOMPARE(table->format().headerRowCount(), 1);
    QCOMPARE(Docx::rowProperties(table, 0).height, 720);
    scene.control()->undo(); // the table and the row page in one step
    QVERIFY(Docx::tableProperties(table).width.type != u"pct");
    QCOMPARE(table->format().headerRowCount(), 0);
    QCOMPARE(Docx::rowProperties(table, 0).height, 0);
    QCOMPARE(cellText(table, 1, 0), QStringLiteral("Cleo")); // the sort stayed
    scene.control()->redo();
    QCOMPARE(table->format().headerRowCount(), 1);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ages.docx"));
    QString error;
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    const QByteArray xml = QindaOffice::Ooxml::Package::load(path)->data(QStringLiteral("word/document.xml"));
    QVERIFY(xml.contains("<w:tblW w:w=\"3000\" w:type=\"pct\"/><w:jc w:val=\"center\"/>"));
    QVERIFY(xml.contains("<w:cantSplit/><w:trHeight w:val=\"720\" w:hRule=\"exact\"/><w:tblHeader/>"));

    // Convert to Text: a row a paragraph, cells separated by tabs.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableToTextAction")));
    QVERIFY(tableIn(scene.doc->textDocument()) == nullptr);
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Name\tAge")));
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("Bea\t30")));
    scene.control()->undo(); // the table back in one step
    QVERIFY(tableIn(scene.doc->textDocument()) != nullptr);
}

void WriteTablesTest::dialogsAtBothWidths()
{
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.insertTable(3, 3);
        QTextTable *table = tableIn(scene.doc->textDocument());
        fill(table, {QStringLiteral("Region|Q1|Q2"), QStringLiteral("North|12|15"), QStringLiteral("South|9|11")});
        // A shaded header row with a double red rule, and a dashed box.
        scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectRowAction")));
        scene.host.bordersController()->applyShading(QColor(0xD9, 0xE2, 0xF3));
        scene.host.bordersController()->setPenStyleIndex(5);
        scene.host.bordersController()->setPenSizeIndex(4);
        scene.host.bordersController()->setPenColor(QColor(0xC0, 0x00, 0x00));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersBottomAction")));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectTableAction")));
        scene.host.bordersController()->setPenStyleIndex(2);
        scene.host.bordersController()->setPenColor(QColor(0x2F, 0x54, 0x96));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersOutsideAction")));
        scene.control()->setTextCursor(table->cellAt(1, 1).firstCursorPosition());
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_table_%1").arg(width));
        for (const auto &[action, name] : {std::pair{"writeTablePropertiesAction", "tablePropertiesDialog"},
                                           std::pair{"writeBordersShadingAction", "bordersShadingDialog"},
                                           std::pair{"writeTableSortAction", "sortDialog"}}) {
            QObject *dialog = openDialog(scene, action, name);
            QVERIFY2(dialog != nullptr, name);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_%1_%2").arg(QLatin1String(name)).arg(width));
            QMetaObject::invokeMethod(dialog, "close");
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
        // The Table Design tab with its pen, shading and borders.
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVariant ok;
        QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, QStringLiteral("tableDesign")));
        QVERIFY(ok.toBool());
        QTRY_VERIFY(scene.item(QStringLiteral("penStyleCombo")) != nullptr);
        QTest::qWait(120);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_table_design_%1").arg(width));
    }
}

// Repeat Header Rows: Qt's layout repeats them at the top of each page the
// table runs onto (QTextTableFormat::headerRowCount), as Word does; the
// HTML export keeps cell shading and borders.
void WriteTablesTest::headerRowsRepeatAndHtmlKeepsTheLook()
{
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    scene.host.insertTable(70, 2);
    QTextTable *table = tableIn(scene.doc->textDocument());
    QStringList rows{QStringLiteral("Heading A|Heading B")};
    for (int r = 1; r < 70; ++r) {
        rows.append(QStringLiteral("Row %1|Value %1").arg(r));
    }
    fill(table, rows);
    QTextDocument *doc = scene.doc->textDocument();
    const qreal page = doc->pageSize().height();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    // The first row on page 2 and where it starts below the page's top.
    const auto firstOnPageTwo = [&] {
        for (int r = 1; r < table->rows(); ++r) {
            const QRectF rect = layout->blockBoundingRect(table->cellAt(r, 0).firstCursorPosition().block());
            if (rect.top() >= page) {
                return std::pair{r, rect.top() - page};
            }
        }
        return std::pair{-1, 0.0};
    };
    const auto [plainRow, plainOffset] = firstOnPageTwo();
    QVERIFY(plainRow > 0);
    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    scene.host.action(QStringLiteral("writeTableRepeatHeaderAction"))->trigger();
    QCOMPARE(table->format().headerRowCount(), 1);
    const auto [headerRow, headerOffset] = firstOnPageTwo();
    QVERIFY(headerRow > 0);
    const qreal headerHeight = layout->blockBoundingRect(table->cellAt(0, 0).firstCursorPosition().block()).height();
    // Page two starts with the repeated heading, then the rows.
    QVERIFY2(headerOffset >= plainOffset + headerHeight * 0.8,
             qPrintable(QStringLiteral("%1 vs %2 + %3").arg(headerOffset).arg(plainOffset).arg(headerHeight)));
    scene.control()->setTextCursor(table->cellAt(qMin(headerRow + 3, table->rows() - 1), 0).firstCursorPosition());
    QTest::qWait(200);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m70_header_repeat"));

    scene.control()->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeTableSelectRowAction")));
    scene.host.bordersController()->applyShading(QColor(0xD9, 0xE2, 0xF3));
    scene.host.bordersController()->setPenStyleIndex(5);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("writeBordersBottomAction")));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("table.html"));
    QString error;
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(file.readAll());
    QVERIFY(html.contains(QStringLiteral("bgcolor=\"#d9e2f3\"")));
    QVERIFY(html.contains(QStringLiteral("border-bottom-style:double")));
}

WRITE_TEST_MAIN(WriteTablesTest)

#include "tst_write_tables.moc"
