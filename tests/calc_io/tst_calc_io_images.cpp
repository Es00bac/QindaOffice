// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io picture gates (M39): floating images round-trip with the bytes,
// the format and the anchor the file carried, land in the package as real
// media parts with rels and an <xdr:pic> anchor, and coexist with charts in
// one drawing. Bytes matter here: QXlsx's own insertImage re-encodes every
// image as PNG, which is what this path exists to avoid.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"

#include "structure.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <KArchiveFile>
#include <KZip>

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoImagesTest : public QObject {
    Q_OBJECT
private slots:
    void imagesRoundTrip();
    void imagesFollowStructuralEdits();
    void picturesAndChartsShareOneDrawing();
};

static QByteArray encodedImage(const char *format, const QColor &fill)
{
    QImage image(24, 16, QImage::Format_RGB32);
    image.fill(fill);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

static QStringList partNames(const QString &path)
{
    QStringList names;
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return names;
    }
    std::function<void(const KArchiveDirectory *, const QString &)> walk =
        [&](const KArchiveDirectory *dir, const QString &prefix) {
            for (const QString &name : dir->entries()) {
                const KArchiveEntry *entry = dir->entry(name);
                const QString full = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
                if (entry->isDirectory()) {
                    walk(static_cast<const KArchiveDirectory *>(entry), full);
                } else {
                    names.append(full);
                }
            }
        };
    walk(zip.directory(), QString());
    return names;
}

static QByteArray readPart(const QString &path, const QString &part)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return {};
    }
    const KArchiveFile *file = zip.directory()->file(part);
    return file != nullptr ? file->data() : QByteArray();
}

void CalcIoImagesTest::imagesRoundTrip()
{
    const QByteArray jpegBytes = encodedImage("JPG", QColor(200, 40, 40));
    QVERIFY(!jpegBytes.isEmpty());

    Workbook wb;
    wb.addSheet(QStringLiteral("Cover"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("Logo below"));
    ImageSpec spec;
    spec.bytes = QSharedPointer<const QByteArray>::create(jpegBytes);
    spec.format = QStringLiteral("jpg");
    spec.sheet = 0;
    spec.anchorRow = 3;
    spec.anchorCol = 2;
    spec.widthPx = 240;
    spec.heightPx = 160;
    wb.images.append(spec);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/picture.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(path);

    // The package shape an external consumer reads.
    const QStringList names = partNames(path);
    QVERIFY2(names.contains(QStringLiteral("xl/media/image1.jpg")), qPrintable(names.join(u' ')));
    const QByteArray media = readPart(path, QStringLiteral("xl/media/image1.jpg"));
    QCOMPARE(media, jpegBytes); // the author's bytes, not a PNG re-encode
    const QString contentTypes = QString::fromUtf8(readPart(path, QStringLiteral("[Content_Types].xml")));
    QVERIFY2(contentTypes.contains(QStringLiteral("image/jpeg")), qPrintable(contentTypes));
    const QString drawing = QString::fromUtf8(readPart(path, QStringLiteral("xl/drawings/drawing1.xml")));
    QVERIFY2(drawing.contains(QStringLiteral("<xdr:pic>")), qPrintable(drawing));
    QVERIFY(drawing.contains(QStringLiteral("a:blip")));
    QVERIFY(drawing.contains(QStringLiteral("<xdr:col>2</xdr:col>")));
    QVERIFY(drawing.contains(QStringLiteral("<xdr:row>3</xdr:row>")));
    // 240 px at 9525 EMU per pixel.
    QVERIFY2(drawing.contains(QStringLiteral("cx=\"2286000\"")), qPrintable(drawing));
    const QString drawingRels =
        QString::fromUtf8(readPart(path, QStringLiteral("xl/drawings/_rels/drawing1.xml.rels")));
    QVERIFY2(drawingRels.contains(QStringLiteral("../media/image1.jpg")), qPrintable(drawingRels));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.images.size(), 1);
    const ImageSpec &read = back.images.at(0);
    QCOMPARE(*read.bytes, jpegBytes);
    QCOMPARE(read.format, QStringLiteral("jpg"));
    QCOMPARE(read.sheet, 0);
    QCOMPARE(read.anchorRow, 3);
    QCOMPARE(read.anchorCol, 2);
    QCOMPARE(read.widthPx, 240);
    QCOMPARE(read.heightPx, 160);
    // The decoded image is still the picture that went in.
    const QImage decoded = QImage::fromData(*read.bytes);
    QCOMPARE(decoded.size(), QSize(24, 16));
}

void CalcIoImagesTest::imagesFollowStructuralEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Cover"));
    wb.addSheet(QStringLiteral("Data"));
    ImageSpec spec;
    spec.bytes = QSharedPointer<const QByteArray>::create(encodedImage("PNG", Qt::blue));
    spec.sheet = 1;
    spec.anchorRow = 5;
    spec.anchorCol = 1;
    spec.widthPx = 80;
    spec.heightPx = 60;
    wb.images.append(spec);

    applyStructureEdit(wb, {StructureOp::InsertRows, 1, 0, 3});
    QCOMPARE(wb.images.at(0).anchorRow, 8);
    applyStructureEdit(wb, {StructureOp::InsertColumns, 1, 0, 2});
    QCOMPARE(wb.images.at(0).anchorCol, 3);
    // An edit on another sheet leaves it alone.
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 4});
    QCOMPARE(wb.images.at(0).anchorRow, 8);
    // Removing the sheet it lives on takes the image with it.
    QVERIFY(removeSheet(wb, 1));
    QCOMPARE(wb.images.size(), 0);
}

// A sheet carrying both kinds of floating object has ONE drawing part with
// both anchors in it — the second writer must merge into what the first
// created, not mint a rival drawing the sheet does not point at.
void CalcIoImagesTest::picturesAndChartsShareOneDrawing()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    for (int row = 0; row < 4; ++row) {
        wb.setCellInput(0, {row, 0}, QStringLiteral("Cat %1").arg(row));
        wb.setCellInput(0, {row, 1}, QString::number(row * 3));
    }
    ChartSpec chart;
    chart.kind = ChartSpec::Kind::Bar;
    chart.title = QStringLiteral("Totals");
    chart.sheet = 0;
    chart.anchorRow = 1;
    chart.anchorCol = 4;
    chart.widthPx = 480;
    chart.heightPx = 288;
    chart.series.append(ChartSeriesSpec{QStringLiteral("First"),
                                        QStringLiteral("'Data'!$A$1:$A$4"),
                                        QStringLiteral("'Data'!$B$1:$B$4")});
    wb.charts.append(chart);

    ImageSpec image;
    image.bytes = QSharedPointer<const QByteArray>::create(encodedImage("PNG", Qt::magenta));
    image.sheet = 0;
    image.anchorRow = 10;
    image.anchorCol = 1;
    image.widthPx = 64;
    image.heightPx = 48;
    wb.images.append(image);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/both.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(path);

    const QStringList names = partNames(path);
    const QStringList drawings = names.filter(QStringLiteral("xl/drawings/drawing"));
    QCOMPARE(drawings.size(), 1);
    const QString drawing = QString::fromUtf8(readPart(path, drawings.at(0)));
    QVERIFY2(drawing.contains(QStringLiteral("<xdr:pic>")), qPrintable(drawing));
    QVERIFY2(drawing.contains(QStringLiteral("graphicFrame")), qPrintable(drawing));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.images.size(), 1);
    QCOMPARE(back.charts.size(), 1);
    QCOMPARE(back.images.at(0).anchorRow, 10);
    QCOMPARE(back.charts.at(0).series.size(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoImagesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_images.moc"
