// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io style gates (M33): the full CellStyle set round-trips on value,
// formula AND empty cells; custom number-format codes survive the write
// (QXlsx 1.5.1 serializes them — this is the proof); theme+tint colours
// resolve through the package palette.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "xlsx_style.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <QtTest>

#include <functional>

#include <xlsxdocument.h>
#include <xlsxformat.h>

using namespace QindaOffice::Calc;

// Rewrites one part inside a saved package (and optionally adds parts):
// KZip writing truncates the archive, so every entry must round-trip
// through the rewrite. Returns false when the part or needle is absent.
static bool patchPackagePart(const QString &path, const QString &part,
                             const QByteArray &needle, const QByteArray &replacement,
                             const QHash<QString, QByteArray> &extraParts = {})
{
    QHash<QString, QByteArray> parts;
    {
        KZip zip(path);
        if (!zip.open(QIODevice::ReadOnly)) {
            return false;
        }
        std::function<void(const KArchiveDirectory *, const QString &)> walk =
            [&](const KArchiveDirectory *dir, const QString &prefix) {
                for (const QString &name : dir->entries()) {
                    const KArchiveEntry *entry = dir->entry(name);
                    const QString full = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
                    if (entry->isDirectory()) {
                        walk(static_cast<const KArchiveDirectory *>(entry), full);
                    } else {
                        parts.insert(full, static_cast<const KArchiveFile *>(entry)->data());
                    }
                }
            };
        walk(zip.directory(), QString());
        zip.close();
    }
    if (!parts.contains(part)) {
        return false;
    }
    QByteArray xml = parts.value(part);
    if (!xml.contains(needle)) {
        return false;
    }
    xml.replace(needle, replacement);
    parts.insert(part, xml);
    for (auto it = extraParts.constBegin(); it != extraParts.constEnd(); ++it) {
        parts.insert(it.key(), it.value());
    }
    KZip out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (!out.writeFile(it.key(), it.value())) {
            return false;
        }
    }
    return out.close();
}

class CalcIoStylesTest : public QObject {
    Q_OBJECT
private slots:
    void fullStyleRoundTripsOnValueFormulaAndEmptyCells();
    void customNumberFormatsRoundTrip();
    void themeColorsResolveToRgb();
    void defaultPaletteIsTheOfficeScheme();
};

static CellStyle fullStyle()
{
    CellStyle style;
    style.numberFormat = QStringLiteral("0.0%;[Red](0.0%);–");
    style.bold = true;
    style.italic = true;
    style.underline = true;
    style.strikeout = true;
    style.fontSize = 13; // QXlsx writes whole points only (D-033)
    style.fontFamily = QStringLiteral("Aptos");
    style.fg = Rgb{0x12, 0x34, 0x56};
    style.bg = Rgb{0xFE, 0xDC, 0xBA};
    style.alignment = 1;
    style.vAlign = 0;
    style.wrap = true;
    style.borders = 0b1011;
    return style;
}

static void compareStyle(const CellStyle &expected, const CellStyle *actual)
{
    QVERIFY(actual != nullptr);
    QCOMPARE(actual->numberFormat, expected.numberFormat);
    QCOMPARE(actual->bold, expected.bold);
    QCOMPARE(actual->italic, expected.italic);
    QCOMPARE(actual->underline, expected.underline);
    QCOMPARE(actual->strikeout, expected.strikeout);
    QCOMPARE(actual->fontSize, expected.fontSize);
    QCOMPARE(actual->fontFamily, expected.fontFamily);
    QCOMPARE(actual->fg.has_value(), expected.fg.has_value());
    if (expected.fg.has_value()) {
        QCOMPARE(int(actual->fg->r), int(expected.fg->r));
        QCOMPARE(int(actual->fg->g), int(expected.fg->g));
        QCOMPARE(int(actual->fg->b), int(expected.fg->b));
    }
    QCOMPARE(actual->bg.has_value(), expected.bg.has_value());
    if (expected.bg.has_value()) {
        QCOMPARE(int(actual->bg->r), int(expected.bg->r));
        QCOMPARE(int(actual->bg->g), int(expected.bg->g));
        QCOMPARE(int(actual->bg->b), int(expected.bg->b));
    }
    QCOMPARE(actual->alignment, expected.alignment);
    QCOMPARE(actual->vAlign, expected.vAlign);
    QCOMPARE(actual->wrap, expected.wrap);
    QCOMPARE(actual->borders, expected.borders);
}

void CalcIoStylesTest::fullStyleRoundTripsOnValueFormulaAndEmptyCells()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Styled"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("42"));
    wb.setCellInput(0, {1, 0}, QStringLiteral("=A1*2"));
    const qint32 id = wb.addStyle(fullStyle());
    sheet.setStyleIdAt({0, 0}, id); // value cell
    sheet.setStyleIdAt({1, 0}, id); // formula cell (kept the default format in v1)
    sheet.setStyleIdAt({2, 0}, id); // empty cell with only a style
    Q_UNUSED(sheet);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/styled.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(path);

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(toText(back.cellValue(0, 1, 0)), QStringLiteral("84"));
    for (const qint32 row : {0, 1, 2}) {
        const qint32 readId = back.sheetAt(0).styleIdAt({row, 0});
        QVERIFY2(readId != 0, qPrintable(QStringLiteral("row %1 lost its style").arg(row)));
        compareStyle(fullStyle(), back.style(readId));
    }
    // The external contract: a solid fill must land in the file's fgColor
    // element — that is the colour Excel and LibreOffice paint. (QXlsx's
    // public foreground setter would have emitted only bgColor and the
    // fill would vanish for every other consumer.)
    {
        KZip zip(path);
        QVERIFY(zip.open(QIODevice::ReadOnly));
        const KArchiveFile *styles = zip.directory()->file(QStringLiteral("xl/styles.xml"));
        QVERIFY(styles != nullptr);
        const QByteArray xml = styles->data();
        QVERIFY2(xml.contains("fgColor rgb=\"FFFEDCBA\""), xml.constData());
    }
}

void CalcIoStylesTest::customNumberFormatsRoundTrip()
{
    // The sample workbook's own codes: built-ins serialize by id, but these
    // must travel as format-code strings.
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Codes"));
    const QStringList codes = {QStringLiteral("0.0%;[Red](0.0%);–"),
                               QStringLiteral("$#,##0.00;[Red]($#,##0.00);–")};
    for (int i = 0; i < codes.size(); ++i) {
        wb.setCellInput(0, {i, 0}, QStringLiteral("1"));
        CellStyle style;
        style.numberFormat = codes.at(i);
        sheet.setStyleIdAt({i, 0}, wb.addStyle(style));
    }
    Q_UNUSED(sheet);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/codes.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(path);

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    for (int i = 0; i < codes.size(); ++i) {
        const CellStyle *style = back.style(back.sheetAt(0).styleIdAt({i, 0}));
        QVERIFY(style != nullptr);
        QCOMPARE(style->numberFormat, codes.at(i));
    }
}

void CalcIoStylesTest::themeColorsResolveToRgb()
{
    // Write a red-font cell through QXlsx, then rewrite the colour as
    // theme="4" (accent1) with a darkening tint — the shape real producers
    // use everywhere — and check the import resolves it through the
    // package's own theme palette (QXlsx writes no theme part, so the test
    // injects one with a recognisable accent1).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/themed.xlsx");
    {
        QXlsx::Document doc;
        QXlsx::Format format;
        format.setFontColor(QColor(255, 0, 0));
        doc.write(1, 1, QStringLiteral("x"), format);
        QVERIFY(doc.saveAs(path));
    }
    const QByteArray themeXml = QByteArrayLiteral(
        "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
        "<a:themeElements><a:clrScheme name=\"Test\">"
        "<a:dk1><a:srgbClr val=\"000000\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"1F497D\"/></a:dk2><a:lt2><a:srgbClr val=\"EEECE1\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"112233\"/></a:accent1>"
        "<a:accent2><a:srgbClr val=\"445566\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"778899\"/></a:accent3>"
        "<a:accent4><a:srgbClr val=\"AABBCC\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"DDEEFF\"/></a:accent5>"
        "<a:accent6><a:srgbClr val=\"001122\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"0000FF\"/></a:hlink>"
        "<a:folHlink><a:srgbClr val=\"800080\"/></a:folHlink>"
        "</a:clrScheme></a:themeElements></a:theme>");
    QVERIFY(patchPackagePart(path, QStringLiteral("xl/styles.xml"), "rgb=\"FFFF0000\"",
                             "theme=\"4\" tint=\"-0.249977111117893\"",
                             {{QStringLiteral("xl/theme/theme1.xml"), themeXml}}));
    // The expected colour: the injected accent1 0x112233, darkened 25%
    // (channel math the resolver implements: v * (1 + tint)).
    const auto tinted = [](int channel, double tint) {
        return qBound(0, qRound(channel * (1.0 + tint)), 255);
    };
    const Rgb expected{quint8(tinted(0x11, -0.249977111117893)),
                       quint8(tinted(0x22, -0.249977111117893)),
                       quint8(tinted(0x33, -0.249977111117893))};

    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const CellStyle *style = back.style(back.sheetAt(0).styleIdAt({0, 0}));
    QVERIFY(style != nullptr);
    QVERIFY(style->fg.has_value());
    QCOMPARE(int(style->fg->r), int(expected.r));
    QCOMPARE(int(style->fg->g), int(expected.g));
    QCOMPARE(int(style->fg->b), int(expected.b));
}

void CalcIoStylesTest::defaultPaletteIsTheOfficeScheme()
{
    const IO::ThemePalette palette = IO::ThemePalette::officeDefault();
    QCOMPARE(palette.colors.size(), 12);
    QCOMPARE(palette.slot(4), QRgb(0xFF4472C4)); // accent1
    QCOMPARE(palette.slot(0), QRgb(0xFFFFFFFF)); // lt1 (background1)
    QCOMPARE(palette.slot(1), QRgb(0xFF000000)); // dk1 (text1)
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoStylesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_styles.moc"
