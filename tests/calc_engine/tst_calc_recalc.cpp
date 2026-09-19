// SPDX-License-Identifier: GPL-3.0-or-later
// Dependency/recalc behavior driven by tests/fixtures/sheets/*.json (plan
// section 7.2): each fixture declares initial cells and a sequence of
// set/expect steps. Cycles must surface as Err::Circular (Err:522 display).

#include "workbook.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace QindaOffice::Calc;

class CalcRecalcTest : public QObject {
    Q_OBJECT
private slots:
    void fixtures_data();
    void fixtures();
    void volatileRecalc();
    void crossSheetDependency();
    void wholeColumnDependency();
    void orderingTheDirtySetStaysCheap();
};

static CellAddr addrFromText(const QString &text)
{
    int i = 0;
    while (!text.at(i).isDigit()) {
        ++i;
    }
    return {text.mid(i).toInt() - 1, colFromName(text.left(i)).value_or(0)};
}

static QString display(const Value &v)
{
    return toText(v);
}

void CalcRecalcTest::fixtures_data()
{
    QTest::addColumn<QString>("path");
    const QDir dir(QStringLiteral(SOURCE_DIR "/../fixtures/sheets"));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        QTest::newRow(qPrintable(file)) << dir.filePath(file);
    }
}

void CalcRecalcTest::fixtures()
{
    QFETCH(QString, path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    Workbook wb;
    wb.addSheet(QStringLiteral("Sheet1"));
    Workbook *wbp = &wb;

    auto applySet = [wbp](const QJsonObject &set) {
        const QString addr = set.value(QStringLiteral("set")).toString();
        const QString to = set.value(QStringLiteral("to")).toString();
        wbp->setCellInput(0, addrFromText(addr), to);
    };

    const QJsonObject cells = root.value(QStringLiteral("cells")).toObject();
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        wb.setCellInput(0, addrFromText(it.key()), it.value().toString());
    }
    const QJsonArray steps = root.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue &stepValue : steps) {
        const QJsonObject step = stepValue.toObject();
        if (step.contains(QStringLiteral("set"))) {
            applySet(step);
            continue;
        }
        const QJsonObject expect = step.value(QStringLiteral("expect")).toObject();
        for (auto it = expect.begin(); it != expect.end(); ++it) {
            const Value got = wb.cellValue(0, addrFromText(it.key()).row, addrFromText(it.key()).col);
            const QString want = it.value().toString();
            if (want.startsWith(QLatin1String("Err:")) || want.startsWith(QLatin1Char('#'))) {
                QVERIFY2(isErr(got), qPrintable(QStringLiteral("%1: wanted error %2, got %3")
                                                    .arg(it.key(), want, display(got))));
                QCOMPARE(display(got), want);
            } else {
                bool ok = false;
                const double num = want.toDouble(&ok);
                if (ok) {
                    QVERIFY2(isNumber(got), qPrintable(QStringLiteral("%1: wanted %2, got %3")
                                                           .arg(it.key(), want, display(got))));
                    QVERIFY2(std::abs(std::get<double>(got) - num) < 1e-9,
                             qPrintable(QStringLiteral("%1: wanted %2, got %3")
                                            .arg(it.key(), want, display(got))));
                } else {
                    QCOMPARE(display(got), want);
                }
            }
        }
    }
}

void CalcRecalcTest::volatileRecalc()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("=RAND()"));
    wb.setCellInput(0, {1, 0}, QStringLiteral("=A1"));
    const Value first = wb.cellValue(0, 0, 0);
    const Value mirrored = wb.cellValue(0, 1, 0);
    QVERIFY(isNumber(first));
    QCOMPARE(display(first), display(mirrored));
    // Editing any cell re-rolls volatile cells.
    wb.setCellInput(0, {5, 5}, QStringLiteral("42"));
    const Value rerolled = wb.cellValue(0, 0, 0);
    QVERIFY(isNumber(rerolled));
}

void CalcRecalcTest::crossSheetDependency()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("First"));
    wb.addSheet(QStringLiteral("Second"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("7"));
    wb.setCellInput(1, {0, 0}, QStringLiteral("=First!A1*2"));
    QCOMPARE(display(wb.cellValue(1, 0, 0)), QStringLiteral("14"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("10"));
    QCOMPARE(display(wb.cellValue(1, 0, 0)), QStringLiteral("20"));
}

void CalcRecalcTest::wholeColumnDependency()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("=SUM(B:B)"));
    wb.setCellInput(0, {1, 1}, QStringLiteral("4"));
    QCOMPARE(display(wb.cellValue(0, 0, 0)), QStringLiteral("4"));
    wb.setCellInput(0, {500, 1}, QStringLiteral("6"));
    QCOMPARE(display(wb.cellValue(0, 0, 0)), QStringLiteral("10"));
}

// Ordering the dirty set, not evaluating it, used to dominate recalc: the
// topological sort rebuilt a band's candidate set and deep-copied each
// candidate's target list once per dirty cell. The shape that exposes it is
// many formulas packed into one row band whose ranges all cover that band —
// then every dirty cell reconstructs the same thousand-entry candidate set.
// A real 100k-cell workbook spent three minutes here to do two seconds of
// arithmetic.
void CalcRecalcTest::orderingTheDirtySetStaysCheap()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Big"));
    constexpr int kValues = 400;
    constexpr int kFormulaRows = 250;   // one 256-row band
    constexpr int kFormulaCols = 12;    // 3000 formulas in that band
    wb.beginBulkLoad();
    for (int row = 0; row < kValues; ++row) {
        wb.setCellInput(0, {row, 0}, QString::number(row));
    }
    for (int row = 0; row < kFormulaRows; ++row) {
        for (int col = 0; col < kFormulaCols; ++col) {
            const int first = (row + col) % 200;
            wb.setCellInput(0, {row, col + 2},
                            QStringLiteral("=SUM(A%1:A%2)").arg(first + 1).arg(first + 60));
        }
    }
    QElapsedTimer timer;
    timer.start();
    wb.endBulkLoad(); // sorts and evaluates all 3000 formulas at once
    const qint64 elapsed = timer.elapsed();
    QVERIFY(isNumber(wb.cellValue(0, 0, 2)));
    // Milliseconds of sorting, not seconds: the budget only has to separate
    // those two worlds, so it is deliberately loose for slower machines.
    QVERIFY2(elapsed < 3000,
             qPrintable(QStringLiteral("ordering 3000 formulas took %1 ms").arg(elapsed)));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcRecalcTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_recalc.moc"
