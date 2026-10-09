// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091): Excel's text overflow through the real QindaCalc window,
// checked pixel by pixel on grabs — left and General text runs on to the
// right over empty cells, right-aligned text to the left, centred text both
// ways; it stops at a cell with a value or a formula (even one showing
// nothing) and at a merged range; wrapped text, Fill-aligned text and
// numbers never spill (a number too wide shows ####); no grid line under
// text that runs on; and the overflow keeps painting while the sheet
// scrolls, past a freeze too. The paper says the same (Export PDF rendered
// through Poppler), and the layout rules themselves are checked on their
// own (calc_text_overflow.h). QQO_GRAB_DIR saves the grid at 1280 and 520.

#include "calc_m78_support.h"

#include "qindacalc/src/calc_text_overflow.h"

#include "number_format.h"
#include "sheet.h"

#include <QFontMetricsF>
#include <QUndoStack>

#include <poppler-qt6.h>

using namespace CalcTest;
using namespace CalcTest::M78;
using QindaOffice::Apps::QindaCalc::SpillDirection;
using QindaOffice::Apps::QindaCalc::StylePatch;
namespace Calc = QindaOffice::Calc;
namespace Overflow = QindaOffice::Apps::QindaCalc;

namespace {

const QString kLabel = QStringLiteral("Quarterly revenue by region");
const QColor kGridLine(0xd8, 0xd8, 0xdc);

bool isInk(const QImage &image, int x, int y)
{
    return image.pixelColor(x, y).lightness() < 150;
}

// The cell's box in window pixels (the offscreen window's DPR is 1).
QRect cellBox(Scene &scene, int row, int col)
{
    SpreadsheetItem *grid = scene.grid;
    const qreal x = grid->headerWidth() + grid->colLeftForTest(col);
    const qreal y = grid->headerHeight() + grid->rowTopForTest(row);
    const qreal w = grid->colLeftForTest(col + 1) - grid->colLeftForTest(col);
    const qreal h = grid->rowTopForTest(row + 1) - grid->rowTopForTest(row);
    const QPointF at = grid->mapToScene(QPointF(x, y));
    return QRect(qRound(at.x()), qRound(at.y()), qRound(w), qRound(h));
}

// The leftmost and rightmost ink in `box` (rows inset from the grid
// lines); {-1, -1} without any.
QPair<int, int> inkSpan(const QImage &image, const QRect &box)
{
    int left = -1;
    int right = -1;
    for (int y = box.top() + 2; y < box.bottom() - 1; ++y) {
        for (int x = qMax(0, box.left()); x <= qMin(image.width() - 1, box.right()); ++x) {
            if (isInk(image, x, y)) {
                left = left < 0 ? x : qMin(left, x);
                right = qMax(right, x);
            }
        }
    }
    return {left, right};
}

bool hasInk(const QImage &image, const QRect &box)
{
    return inkSpan(image, box).first >= 0;
}

// Whether the grid line at x is drawn anywhere down the row's box.
bool gridLineAt(const QImage &image, int x, const QRect &row)
{
    for (int y = row.top() + 2; y < row.bottom() - 1; ++y) {
        if (image.pixelColor(x, y).rgb() == kGridLine.rgb()) {
            return true;
        }
    }
    return false;
}

void setStyle(Scene &scene, const Calc::CellRange &range, const StylePatch &patch)
{
    scene.doc->restyleRange(range, patch, QStringLiteral("Style"));
}

StylePatch aligned(int alignment)
{
    StylePatch patch;
    patch.alignment = alignment;
    return patch;
}

StylePatch wrapped()
{
    StylePatch patch;
    patch.wrap = true;
    return patch;
}

StylePatch formatted(const QString &code)
{
    StylePatch patch;
    patch.numberFormat = code;
    return patch;
}

StylePatch filled(const Calc::Rgb &colour)
{
    StylePatch patch;
    patch.bg = std::optional<Calc::Rgb>(colour);
    return patch;
}

qreal labelWidth(Scene &scene, const QString &text)
{
    QFont font = scene.grid->cellFont();
    font.setPointSizeF(font.pointSizeF() * scene.grid->zoom());
    return QFontMetricsF(font).horizontalAdvance(text);
}

QImage settledShot(Scene &scene)
{
    QTest::qWait(150);
    return scene.window->grabWindow();
}

} // namespace

class CalcOverflowUiTest final : public QObject {
    Q_OBJECT

    static void prepare(Scene &scene)
    {
        QList<qint32> columns;
        for (qint32 c = 0; c < 30; ++c) {
            columns.append(c);
        }
        scene.doc->setColumnWidths(columns, 80);
    }

private slots:
    void textRunsOnOverEmptyNeighboursAndStops()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        prepare(scene);
        type(scene, 0, 0, kLabel); // A1 runs on over B1 and into C1
        type(scene, 1, 0, kLabel); // A2: B2 holds a number
        type(scene, 1, 1, QStringLiteral("7"));
        type(scene, 2, 0, kLabel); // A3: B3:C3 are merged
        scene.doc->mergeCells({{2, 1}, {2, 2}});
        type(scene, 3, 0, kLabel); // A4: B4 is a formula showing nothing
        type(scene, 3, 1, QStringLiteral("=\"\""));
        type(scene, 4, 0, kLabel); // A5: filled neighbours do not stop it
        setStyle(scene, {{4, 0}, {4, 3}}, filled(Calc::Rgb{0xff, 0xf2, 0xcc}));
        scene.grid->setCursorCell(18, 9); // the cursor's fill out of the way
        const QImage shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_1280"));

        const qreal width = labelWidth(scene, kLabel);
        const QRect a1 = cellBox(scene, 0, 0);
        const QRect b1 = cellBox(scene, 0, 1);
        QVERIFY2(width > a1.width() + b1.width() - 8, "the label must be long enough to reach C1");
        // From just inside A1 (the row headers' edge is dark on a dark theme).
        const QRect row1(a1.left() + 1, a1.top(), a1.width() * 5, a1.height());
        const auto [left, right] = inkSpan(shot, row1);
        QVERIFY2(left >= a1.left() + 2 && left <= a1.left() + 8, qPrintable(QStringLiteral("starts at %1").arg(left - a1.left())));
        QVERIFY2(qAbs(right - (a1.left() + 4 + width)) <= 4,
                 qPrintable(QStringLiteral("ends at %1, the label at %2").arg(right - a1.left()).arg(4 + width)));
        // No grid line under the text, while the empty row below keeps its.
        const QRect a7 = cellBox(scene, 6, 0);
        bool underText = false;
        bool inEmptyRow = false;
        for (int dx = 0; dx <= 2; ++dx) {
            underText = underText || gridLineAt(shot, a1.right() + dx, a1);
            inEmptyRow = inEmptyRow || gridLineAt(shot, a1.right() + dx, a7);
        }
        QVERIFY2(!underText, "a grid line under the overflowing text");
        QVERIFY2(inEmptyRow, "the empty row lost its grid line");

        // Stopped by a number, a merged range and a formula showing nothing.
        const QRect b2 = cellBox(scene, 1, 1);
        QVERIFY2(!hasInk(shot, QRect(b2.left() + 1, b2.top(), b2.width() / 2, b2.height())), "A2 ran over B2");
        const QRect b3 = cellBox(scene, 2, 1);
        QVERIFY2(!hasInk(shot, QRect(b3.left() + 1, b3.top(), b3.width() * 2 - 2, b3.height())), "A3 ran into a merge");
        const QRect b4 = cellBox(scene, 3, 1);
        QVERIFY2(!hasInk(shot, QRect(b4.left() + 1, b4.top(), b4.width() - 2, b4.height())), "A4 ran over a formula");
        // A filled row: the text runs on over the fills.
        const QRect b5 = cellBox(scene, 4, 1);
        QVERIFY2(hasInk(shot, QRect(b5.left() + 1, b5.top(), b5.width() - 2, b5.height())), "A5 stopped at a fill");
    }

    void rightAlignedAndCentredTextSpill()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        prepare(scene);
        type(scene, 0, 4, kLabel); // E1 right-aligned: runs left
        setStyle(scene, {{0, 4}, {0, 4}}, aligned(2));
        type(scene, 1, 4, kLabel); // E2 centred: both ways
        setStyle(scene, {{1, 4}, {1, 4}}, aligned(1));
        type(scene, 2, 4, kLabel); // E3 centred, D3 taken: cut on the left only
        type(scene, 2, 3, QStringLiteral("x"));
        setStyle(scene, {{2, 4}, {2, 4}}, aligned(1));
        scene.grid->setCursorCell(18, 12);
        const QImage shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_aligned_1280"));
        const qreal width = labelWidth(scene, kLabel);

        const QRect e1 = cellBox(scene, 0, 4);
        const auto [rl, rr] = inkSpan(shot, QRect(e1.left() - 3 * e1.width(), e1.top(), 4 * e1.width(), e1.height()));
        QVERIFY2(qAbs(rr - (e1.right() - 4)) <= 4, qPrintable(QStringLiteral("right edge %1").arg(rr - e1.right())));
        QVERIFY2(qAbs(rl - (e1.right() - 4 - width)) <= 4, qPrintable(QStringLiteral("left edge %1").arg(rl - e1.left())));

        const QRect e2 = cellBox(scene, 1, 4);
        const auto [cl, cr] = inkSpan(shot, QRect(e2.left() - 2 * e2.width(), e2.top(), 5 * e2.width(), e2.height()));
        const qreal centre = e2.left() + e2.width() / 2.0;
        QVERIFY2(qAbs((cl + cr) / 2.0 - centre) <= 4, qPrintable(QStringLiteral("centre %1 vs %2").arg((cl + cr) / 2.0).arg(centre)));
        QVERIFY(cl < e2.left() && cr > e2.right());

        // D3 holds "x": the centred text keeps its place, cut at E3's left
        // edge, and its right half runs on as E2's does.
        const QRect d3 = cellBox(scene, 2, 3);
        QVERIFY2(!hasInk(shot, QRect(d3.left() + d3.width() / 2, d3.top(), d3.width() / 2 - 1, d3.height())),
                 "the centred text ran over D3");
        const QRect e3 = cellBox(scene, 2, 4);
        const auto [xl, xr] = inkSpan(shot, QRect(e3.left() + 1, e3.top(), 3 * e3.width(), e3.height()));
        QVERIFY(xl >= e3.left() + 1);
        QVERIFY2(qAbs(xr - cr) <= 2, "the right half runs on as in E2");
    }

    void wrappedNumbersAndFillNeverSpill()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        prepare(scene);
        type(scene, 0, 1, kLabel); // B1 wrapped: stays in its cell, wraps
        setStyle(scene, {{0, 1}, {0, 1}}, wrapped());
        scene.doc->setRowHeights({0}, 60);
        type(scene, 1, 2, QStringLiteral("123456789012")); // C2: a number too wide shows ####
        setStyle(scene, {{1, 2}, {1, 2}}, formatted(QStringLiteral("#,##0")));
        type(scene, 2, 0, QStringLiteral("ab")); // A3: Fill repeats it across the cell
        setStyle(scene, {{2, 0}, {2, 0}}, aligned(Overflow::kAlignFill));
        type(scene, 3, 2, QStringLiteral("TRUE")); // C4: a boolean sits in the middle
        scene.grid->setCursorCell(18, 12);
        const QImage shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_never_1280"));

        const QRect c1 = cellBox(scene, 0, 2);
        QVERIFY2(!hasInk(shot, QRect(c1.left() + 1, c1.top(), c1.width() - 2, c1.height())), "wrapped text spilled");
        // It wraps: ink on more than one line of the tall row.
        const QRect b1 = cellBox(scene, 0, 1);
        QVERIFY2(hasInk(shot, QRect(b1.left() + 1, b1.top() + 22, b1.width() - 2, 16)), "wrapped text did not wrap");
        const QRect b2 = cellBox(scene, 1, 1);
        const QRect c2 = cellBox(scene, 1, 2);
        QVERIFY2(!hasInk(shot, QRect(b2.left() + 1, b2.top(), b2.width() - 2, b2.height())), "a number spilled");
        const auto [nl, nr] = inkSpan(shot, c2);
        QVERIFY2(nl >= c2.left() && nr <= c2.right() && nr - nl > c2.width() / 2, "#### fills the cell");
        const QRect a3 = cellBox(scene, 2, 0);
        const QRect b3 = cellBox(scene, 2, 1);
        const auto [fl, fr] = inkSpan(shot, a3);
        QVERIFY2(fl >= 0 && fr - fl > a3.width() / 2, "Fill repeats the text across the cell");
        QVERIFY2(!hasInk(shot, QRect(b3.left() + 1, b3.top(), b3.width() - 2, b3.height())), "Fill spilled");
        const QRect c4 = cellBox(scene, 3, 2);
        const auto [bl, br] = inkSpan(shot, c4);
        QVERIFY2(qAbs((bl + br) / 2.0 - (c4.left() + c4.width() / 2.0)) <= 3, "TRUE is centred");
    }

    void overflowKeepsPaintingWhileScrolling()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        prepare(scene);
        const QString longLabel = kLabel + QStringLiteral(" — and the long tail that keeps going");
        type(scene, 2, 0, longLabel); // A3, scrolled out of view
        scene.grid->setCursorCell(18, 8);
        scene.grid->setScrollX(100); // A and the start of B are gone
        QImage shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_scrolled_1280"));
        const QList<qint32> visible = scene.grid->visibleColumns();
        QVERIFY2(!visible.contains(0), "column A must be scrolled away");
        const QRect row3 = cellBox(scene, 2, visible.first());
        const QPointF viewLeft = scene.grid->mapToScene(QPointF(scene.grid->headerWidth(), 0));
        const auto [sl, sr] = inkSpan(shot, QRect(int(viewLeft.x()), row3.top(), 400, row3.height()));
        QVERIFY2(sl >= 0 && sl <= viewLeft.x() + 30, "the tail of A3 shows at the left edge");
        const qreal expectedEnd = cellBox(scene, 2, 0).left() + 4 + labelWidth(scene, longLabel);
        QVERIFY2(qAbs(sr - expectedEnd) <= 4, qPrintable(QStringLiteral("ends at %1, expected %2").arg(sr).arg(expectedEnd)));

        // A right-aligned text just past the right edge shows its head.
        const qint32 beyond = scene.grid->visibleColumns().last() + 1;
        type(scene, 5, beyond, longLabel);
        setStyle(scene, {{5, beyond}, {5, beyond}}, aligned(2));
        shot = settledShot(scene);
        const QPointF viewRight = scene.grid->mapToScene(QPointF(scene.grid->width(), 0));
        const QRect row6 = cellBox(scene, 5, visible.first());
        QVERIFY2(hasInk(shot, QRect(int(viewRight.x()) - 60, row6.top(), 58, row6.height())),
                 "the head of a text beyond the right edge shows");

        // Past a freeze: column A frozen, B's text scrolled behind it still
        // shows in the scrolling part, never over the frozen column.
        scene.doc->setFreeze(0, 1);
        type(scene, 8, 1, longLabel); // B9
        scene.grid->setScrollX(100);
        shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_frozen_1280"));
        const QRect a9 = cellBox(scene, 8, 0);
        QVERIFY2(!hasInk(shot, QRect(a9.left() + 1, a9.top(), a9.width() - 3, a9.height())), "B9 painted over the frozen A9");
        QVERIFY2(hasInk(shot, QRect(a9.right() + 3, a9.top(), 60, a9.height())), "B9's tail shows past the freeze");
    }

    void paperAgreesWithTheScreen()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        prepare(scene);
        type(scene, 0, 0, kLabel);                 // A1 runs on
        type(scene, 1, 0, kLabel);                 // A2 stopped by B2
        type(scene, 1, 1, QStringLiteral("7"));
        type(scene, 2, 2, QStringLiteral("123456789012")); // C3 ####
        setStyle(scene, {{2, 2}, {2, 2}}, formatted(QStringLiteral("#,##0")));
        type(scene, 3, 4, QStringLiteral("123456789012")); // E4 General in a narrow column
        scene.doc->setColumnWidths({4}, 50);
        type(scene, 4, 4, kLabel);                 // E5 right-aligned: runs left
        setStyle(scene, {{4, 4}, {4, 4}}, aligned(2));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = (qEnvironmentVariableIsSet("QQO_GRAB_DIR") ? qEnvironmentVariable("QQO_GRAB_DIR") : dir.path())
            + QStringLiteral("/qindacalc_m82_overflow.pdf");
        QVERIFY(scene.host.exportPdf(path));
        std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
        QVERIFY(pdf != nullptr && pdf->numPages() >= 1);
        std::unique_ptr<Poppler::Page> page(pdf->page(0));
        const QString text = page->text(QRectF());
        QVERIFY2(text.contains(QStringLiteral("####")), qPrintable(text.left(300)));
        QVERIFY2(text.contains(QStringLiteral("1E+11")) || text.contains(QStringLiteral("1.2E+11")), qPrintable(text.left(300)));
        // At 72 dpi a pixel is a point: Normal margins (0.7 in, 0.75 in),
        // 80 px columns are 60 pt, 20 px rows 15 pt.
        const QImage image = page->renderToImage(72, 72);
        const auto inkIn = [&image](qreal x0, qreal x1, qreal y0, qreal y1) {
            for (int y = int(y0) + 2; y < int(y1) - 1; ++y) {
                for (int x = int(x0); x < int(x1); ++x) {
                    if (image.pixelColor(x, y).lightness() < 150) {
                        return true;
                    }
                }
            }
            return false;
        };
        const qreal left = 0.7 * 72;
        const qreal top = 0.75 * 72;
        QVERIFY2(inkIn(left + 62, left + 118, top, top + 15), "A1 runs on over B1 on paper");
        QVERIFY2(!inkIn(left + 62, left + 90, top + 15, top + 30), "A2 stops at B2 on paper");
        // E5 (x 240..277.5 pt) runs left over D5 (180..240).
        QVERIFY2(inkIn(left + 182, left + 238, top + 60, top + 75), "E5 runs left on paper");
    }

    void layoutRulesOnTheirOwn()
    {
        // Ten units a character.
        const auto measure = [](const QString &text) { return 10.0 * text.size(); };
        QCOMPARE(Overflow::fittedNumber(123456789012.0, QStringLiteral("123456789012"), true, 60, measure), QStringLiteral("1E+11"));
        QCOMPARE(Overflow::fittedNumber(123456789012.0, QStringLiteral("123456789012"), true, 70, measure), QStringLiteral("1.2E+11"));
        QCOMPARE(Overflow::fittedNumber(123456789012.0, QStringLiteral("123456789012"), true, 80, measure), QStringLiteral("1.23E+11"));
        QCOMPARE(Overflow::fittedNumber(3.14159, QStringLiteral("3.14159"), true, 40, measure), QStringLiteral("3.14"));
        QCOMPARE(Overflow::fittedNumber(1234.5, QStringLiteral("1,234.50"), false, 45, measure), QStringLiteral("####"));
        QCOMPARE(Overflow::fittedNumber(0.000012345, QStringLiteral("1.2345e-05"), true, 60, measure), QStringLiteral("1E-05"));
        QCOMPARE(Overflow::fittedNumber(42.0, QStringLiteral("42"), true, 20, measure), QStringLiteral("42"));
        QCOMPARE(Overflow::filledText(QStringLiteral("ab"), 70, measure), QStringLiteral("ababab"));
        QCOMPARE(Overflow::filledText(QStringLiteral("abcdef"), 30, measure), QStringLiteral("abcdef"));

        Calc::Workbook book;
        book.addSheet(QStringLiteral("Sheet1"));
        book.setCellInput(0, {0, 2}, QStringLiteral("label"));
        book.setCellInput(0, {0, 5}, QStringLiteral("stop"));
        const Calc::Sheet &sheet = book.sheetAt(0);
        const Overflow::ColumnWidth width = [](qint32) { return 50.0; };
        // Right: C over D and E, stopped by F.
        auto span = Overflow::spillSpan(sheet, {0, 2}, SpillDirection::Right, 180, width);
        QCOMPARE(span.first, 2);
        QCOMPARE(span.last, 4);
        // Only what it needs.
        span = Overflow::spillSpan(sheet, {0, 2}, SpillDirection::Right, 90, width);
        QCOMPARE(span.last, 3);
        // Left: down to A.
        span = Overflow::spillSpan(sheet, {0, 2}, SpillDirection::Left, 400, width);
        QCOMPARE(span.first, 0);
        // Both: half each way.
        span = Overflow::spillSpan(sheet, {0, 2}, SpillDirection::Both, 150, width);
        QCOMPARE(span.first, 1);
        QCOMPARE(span.last, 3);
        // Fits: no spill at all.
        span = Overflow::spillSpan(sheet, {0, 2}, SpillDirection::Right, 40, width);
        QCOMPARE(span.last, 2);
        // A merge stops it.
        Calc::Workbook merged;
        merged.addSheet(QStringLiteral("Sheet1"));
        merged.setCellInput(0, {0, 0}, QStringLiteral("label"));
        merged.sheetAt(0).addMerge({{0, 2}, {1, 3}});
        span = Overflow::spillSpan(merged.sheetAt(0), {0, 0}, SpillDirection::Right, 400, width);
        QCOMPARE(span.last, 1);
        QCOMPARE(Overflow::nearestStop(sheet, 0, 4, -1, 0, width, 1000), 2);
        QCOMPARE(Overflow::nearestStop(sheet, 0, 6, 1, 100, width, 200), -1);
        // Which values spill, and which way.
        Calc::CellStyle wrapped;
        wrapped.wrap = true;
        QCOMPARE(Overflow::horizontalLayout(Calc::Value(QStringLiteral("x")), nullptr, false).spill, SpillDirection::Right);
        QCOMPARE(Overflow::horizontalLayout(Calc::Value(QStringLiteral("x")), &wrapped, false).spill, SpillDirection::None);
        QCOMPARE(Overflow::horizontalLayout(Calc::Value(QStringLiteral("x")), nullptr, true).spill, SpillDirection::None);
        QCOMPARE(Overflow::horizontalLayout(Calc::Value(3.0), nullptr, false).spill, SpillDirection::None);
        QCOMPARE(Overflow::horizontalLayout(Calc::Value(true), nullptr, false).align, Qt::AlignHCenter);
    }

    void squeezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        prepare(scene);
        type(scene, 0, 0, kLabel);
        type(scene, 1, 2, kLabel);
        setStyle(scene, {{1, 2}, {1, 2}}, aligned(1));
        scene.grid->setCursorCell(12, 3);
        const QImage shot = settledShot(scene);
        grab(scene, QStringLiteral("qindacalc_m82_overflow_520"));
        const QRect b1 = cellBox(scene, 0, 1);
        QVERIFY(hasInk(shot, QRect(b1.left() + 1, b1.top(), b1.width() - 2, b1.height())));
    }
};

CALC_TEST_MAIN(CalcOverflowUiTest)

#include "tst_calc_overflow_ui.moc"
