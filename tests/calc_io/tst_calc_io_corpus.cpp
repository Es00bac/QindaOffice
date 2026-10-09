// SPDX-License-Identifier: GPL-3.0-or-later
// Real-workbook gate for calc_io (M69, D-078), the spreadsheet twin of
// tst_docx_corpus: reads every .xlsx/.xlsm listed in the file
// QQO_XLSX_CORPUS names (one path per line) with readXlsxFile, saves each
// with writeXlsxFile — exactly the two calls QindaCalc's Open and Save make
// (SpreadsheetDocument::loadStaged/save), keeping the extension so an
// .xlsm stays macro-enabled — and reads the result again. With
// QQO_OOXML_SCHEMA_DIR set every written package is validated against the
// ECMA-376 schemas; only errors the save ADDS count (a carried part keeps
// whatever its source had).
//
// AGENT-GUARD: it runs over the owner's private files, which never leave
// the laptop. It prints aggregate counts only — never a file name, a sheet
// name, a formula or a cell's content — and failures are numbered, not
// named. Skips when QQO_XLSX_CORPUS is unset.

#include "calc_io_schema.h"

#include "xlsx_bridge.h"

#include "ooxml_package.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;

class CalcIoCorpusTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripsEveryWorkbook();
};

namespace {

struct BookStats {
    qint64 sheets = 0;
    qint64 cells = 0;    // cells with a value or a formula
    qint64 formulas = 0;
    qint64 charts = 0;
    qint64 pictures = 0;
    qint64 tables = 0;
    qint64 comments = 0;
    qint64 carriedParts = 0; // parts of the reduced source copy a save carries from
};

qint64 carriedParts(const Workbook &wb)
{
    if (wb.source.carried.isEmpty()) {
        return 0;
    }
    const auto package = QindaOffice::Ooxml::Package::fromData(wb.source.carried);
    return package ? qint64(package->partNames().size()) : 0;
}

BookStats statsOf(const Workbook &wb)
{
    BookStats stats;
    stats.sheets = wb.sheetCount();
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        for (const Cell &cell : sheet.cells()) {
            if (cell.hasFormula()) {
                ++stats.formulas;
            }
            if (cell.hasFormula() || !isBlank(cell.value)) {
                ++stats.cells;
            }
        }
        stats.tables += sheet.tables().size();
        stats.comments += sheet.comments().size();
    }
    stats.charts = wb.charts.size();
    stats.pictures = wb.images.size();
    stats.carriedParts = carriedParts(wb);
    return stats;
}

bool sameValue(const Value &a, const Value &b)
{
    if (isNumber(a) && isNumber(b)) {
        const double x = std::get<double>(a);
        const double y = std::get<double>(b);
        return x == y || std::abs(x - y) <= 1e-9 * std::max(std::abs(x), std::abs(y));
    }
    return a == b;
}

// Cells (of the sheets both books have, by position) whose formula text or
// value differs; volatile formulas (NOW, RAND, …) compare by formula only.
qint64 changedCells(const Workbook &before, const Workbook &after, qint64 *formulasChanged)
{
    qint64 changed = 0;
    for (qint32 s = 0; s < qMin(before.sheetCount(), after.sheetCount()); ++s) {
        const Sheet &a = before.sheetAt(s);
        const Sheet &b = after.sheetAt(s);
        QSet<quint64> keys;
        for (auto it = a.cells().constBegin(); it != a.cells().constEnd(); ++it) {
            keys.insert(it.key());
        }
        for (auto it = b.cells().constBegin(); it != b.cells().constEnd(); ++it) {
            keys.insert(it.key());
        }
        for (const quint64 key : std::as_const(keys)) {
            const auto foundX = a.cells().constFind(key);
            const auto foundY = b.cells().constFind(key);
            const Cell *x = foundX != a.cells().constEnd() ? &foundX.value() : nullptr;
            const Cell *y = foundY != b.cells().constEnd() ? &foundY.value() : nullptr;
            const QString fx = x != nullptr ? x->formula : QString();
            const QString fy = y != nullptr ? y->formula : QString();
            const Value vx = x != nullptr ? x->value : Value();
            const Value vy = y != nullptr ? y->value : Value();
            const bool volatileCell = (x != nullptr && x->isVolatile) || (y != nullptr && y->isVolatile);
            if (fx != fy) {
                ++*formulasChanged;
                ++changed;
            } else if (!volatileCell && !sameValue(vx, vy)) {
                ++changed;
            }
        }
    }
    return changed;
}

QStringList sheetNames(const Workbook &wb)
{
    QStringList names;
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        names.append(wb.sheetName(s));
    }
    return names;
}

// The schema messages of a package, line by line, without line numbers —
// so the source's own errors can be subtracted from the save's.
QSet<QString> schemaMessages(const QindaOffice::Ooxml::Package &package)
{
    QSet<QString> out;
    static const QRegularExpression kLine(QStringLiteral("^line:?\\d*:? *"));
    for (const QString &part : QindaOffice::Test::validateSpreadsheetPackage(package)) {
        for (QString line : part.section(QStringLiteral(": "), 1).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            out.insert(line.remove(kLine));
        }
    }
    return out;
}

} // namespace

void CalcIoCorpusTest::roundTripsEveryWorkbook()
{
    const QString listPath = qEnvironmentVariable("QQO_XLSX_CORPUS");
    if (listPath.isEmpty()) {
        QSKIP("QQO_XLSX_CORPUS unset");
    }
    QFile list(listPath);
    QVERIFY(list.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList paths = QString::fromUtf8(list.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const bool validate = QindaOffice::Test::schemaValidationAvailable();
    qint64 read = 0;
    qint64 readFailed = 0;
    qint64 written = 0;
    qint64 writeFailed = 0;
    qint64 rereadFailed = 0;
    qint64 openWarnings = 0;
    qint64 saveWarnings = 0;
    qint64 sheetsChanged = 0;
    qint64 cellCountChanged = 0;
    qint64 formulaCountChanged = 0;
    qint64 booksWithCellChanges = 0;
    qint64 cellsChanged = 0;
    qint64 formulaTextsChanged = 0;
    qint64 chartsChanged = 0;
    qint64 picturesChanged = 0;
    qint64 tablesChanged = 0;
    qint64 commentsChanged = 0;
    qint64 carriedChanged = 0;
    qint64 invalid = 0;
    qint64 slowestMs = 0;
    QMap<QString, int> schemaErrors;
    BookStats total;
    QStringList failures;
    QElapsedTimer all;
    all.start();
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const QString path = paths.at(i).trimmed();
        Workbook wb;
        QString error;
        QStringList warnings;
        QElapsedTimer timer;
        timer.start();
        if (!IO::readXlsxFile(path, &wb, &error, &warnings)) {
            ++readFailed;
            failures.append(QStringLiteral("#%1 read failed").arg(i + 1));
            continue;
        }
        ++read;
        openWarnings += warnings.isEmpty() ? 0 : 1;
        const BookStats before = statsOf(wb);
        total.sheets += before.sheets;
        total.cells += before.cells;
        total.formulas += before.formulas;
        total.charts += before.charts;
        total.pictures += before.pictures;
        total.tables += before.tables;
        total.comments += before.comments;
        total.carriedParts += before.carriedParts;

        const bool macros = path.endsWith(QLatin1String(".xlsm"), Qt::CaseInsensitive);
        const QString out = scratch.filePath(QStringLiteral("book%1.%2").arg(i + 1).arg(macros ? "xlsm" : "xlsx"));
        QStringList saveNotes;
        if (!IO::writeXlsxFile(wb, out, &error, &saveNotes)) {
            ++writeFailed;
            failures.append(QStringLiteral("#%1 save failed").arg(i + 1));
            continue;
        }
        ++written;
        saveWarnings += saveNotes.isEmpty() ? 0 : 1;
        Workbook back;
        if (!IO::readXlsxFile(out, &back, &error)) {
            ++rereadFailed;
            failures.append(QStringLiteral("#%1 re-read failed").arg(i + 1));
            continue;
        }
        slowestMs = qMax(slowestMs, timer.elapsed());
        const BookStats after = statsOf(back);
        sheetsChanged += sheetNames(wb) != sheetNames(back) ? 1 : 0;
        cellCountChanged += before.cells != after.cells ? 1 : 0;
        formulaCountChanged += before.formulas != after.formulas ? 1 : 0;
        chartsChanged += before.charts != after.charts ? 1 : 0;
        picturesChanged += before.pictures != after.pictures ? 1 : 0;
        tablesChanged += before.tables != after.tables ? 1 : 0;
        commentsChanged += before.comments != after.comments ? 1 : 0;
        carriedChanged += before.carriedParts != after.carriedParts ? 1 : 0;
        const qint64 changed = changedCells(wb, back, &formulaTextsChanged);
        cellsChanged += changed;
        booksWithCellChanges += changed > 0 ? 1 : 0;
        if (validate) {
            const auto source = QindaOffice::Ooxml::Package::load(path);
            const auto saved = QindaOffice::Ooxml::Package::load(out);
            const QSet<QString> sourceErrors = source ? schemaMessages(*source) : QSet<QString>();
            const QStringList added = saved ? (schemaMessages(*saved) - sourceErrors).values()
                                            : QStringList{QStringLiteral("the saved package does not load")};
            if (!added.isEmpty()) {
                ++invalid;
                for (const QString &message : added) {
                    // Aggregated by the schema's message (element names and
                    // types), which carries no cell content.
                    QString key = message.section(QStringLiteral("Element '"), 1).left(200);
                    key = key.isEmpty() ? message.left(160) : key;
                    // A rejected value is masked: it could be a cell's.
                    static const QRegularExpression kValue(QStringLiteral("'[^'{}]*' is not"));
                    key.replace(kValue, QStringLiteral("'…' is not"));
                    schemaErrors[key] += 1;
                }
            }
        }
        QFile::remove(out);
    }
    qInfo().noquote() << QStringLiteral(
        "corpus: %1 read, %2 failed, %3 ms total, slowest round trip %4 ms; %5 sheets, %6 cells, %7 formulas, "
        "%8 charts, %9 pictures, %10 tables, %11 notes, %12 carried parts; %13 with open warnings")
        .arg(read).arg(readFailed).arg(all.elapsed()).arg(slowestMs).arg(total.sheets).arg(total.cells)
        .arg(total.formulas).arg(total.charts).arg(total.pictures).arg(total.tables).arg(total.comments)
        .arg(total.carriedParts).arg(openWarnings);
    qInfo().noquote() << QStringLiteral(
        "round trip: %1 written, %2 failed, %3 re-reads failed, %4 with save warnings; workbooks whose sheets "
        "changed %5, cell count %6, formula count %7, charts %8, pictures %9, tables %10, notes %11, carried "
        "parts %12; %13 cells changed (%14 formula texts) in %15 workbooks; %16 schema-invalid%17")
        .arg(written).arg(writeFailed).arg(rereadFailed).arg(saveWarnings).arg(sheetsChanged).arg(cellCountChanged)
        .arg(formulaCountChanged).arg(chartsChanged).arg(picturesChanged).arg(tablesChanged).arg(commentsChanged)
        .arg(carriedChanged).arg(cellsChanged).arg(formulaTextsChanged).arg(booksWithCellChanges).arg(invalid)
        .arg(validate ? QString() : QStringLiteral(" (validation off: set QQO_OOXML_SCHEMA_DIR)"));
    for (auto it = schemaErrors.cbegin(); it != schemaErrors.cend(); ++it) {
        qInfo().noquote() << QStringLiteral("  %1x %2").arg(it.value()).arg(it.key());
    }
    for (const QString &failure : std::as_const(failures)) {
        qInfo().noquote() << failure;
    }
    QCOMPARE(readFailed, 0);
    QCOMPARE(writeFailed, 0);
    QCOMPARE(rereadFailed, 0);
}

// A GUI application: real workbooks hold pictures and fonts QXlsx decodes.
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    CalcIoCorpusTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_corpus.moc"
