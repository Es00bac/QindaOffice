// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package reader for a sheet's drawing (xlsx_read_xml.h): one walk
// over the anchors yields both floating objects the engine models — charts
// (QXlsx's chart support is write-only, so chart XML is parsed here: plot
// type and grouping, title, per-series tx/cat/val references) and pictures
// (the blip's media bytes, kept in the format the file used). Producers may
// put either on any path (the sample uses xl/drawings/charts/), so parts are
// always resolved through the rels, never by convention.

#include "xlsx_read_xml.h"

#include "xlsx_parts.h"

#include "workbook.h"

#include <QSharedPointer>
#include <QSize>
#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

QString relsPathFor(const QString &part)
{
    const int slash = part.lastIndexOf(QLatin1Char('/'));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash)
        + QStringLiteral(".rels");
}

struct Anchor {
    int fromCol = 0;
    int fromRow = 0;
    int toCol = -1; // -1 with ext: oneCellAnchor (size from ext)
    int toRow = -1;
    int widthPx = 0;
    int heightPx = 0;
    QString chartRid;
    QString picRid; // a:blip r:embed — set when the anchor holds a picture
};

// One twoCellAnchor/oneCellAnchor element; the reader must be positioned at
// its start and is left past its end. `kind` must be a compile-time literal
// — a QStringView captured from the reader dangles once it advances.
Anchor readAnchor(QXmlStreamReader &reader, const QLatin1String &kind)
{
    Anchor anchor;
    enum class Section { None, From, To } section = Section::None;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("from")) {
                section = Section::From;
            } else if (name == QLatin1String("to")) {
                section = Section::To;
            } else if (name == QLatin1String("chart")) {
                anchor.chartRid = reader.attributes().value(QLatin1String("r:id")).toString();
            } else if (name == QLatin1String("blip")) {
                anchor.picRid = reader.attributes().value(QLatin1String("r:embed")).toString();
            } else if (name == QLatin1String("ext")) {
                // oneCellAnchor's size, EMU (9525 per px).
                anchor.widthPx = reader.attributes().value(QLatin1String("cx")).toInt() / 9525;
                anchor.heightPx = reader.attributes().value(QLatin1String("cy")).toInt() / 9525;
            } else if (name == QLatin1String("col") || name == QLatin1String("row")) {
                const int value = reader.readElementText().toInt();
                if (section == Section::From) {
                    if (name == QLatin1String("col")) {
                        anchor.fromCol = value;
                    } else {
                        anchor.fromRow = value;
                    }
                } else if (section == Section::To) {
                    if (name == QLatin1String("col")) {
                        anchor.toCol = value;
                    } else {
                        anchor.toRow = value;
                    }
                }
            }
            continue;
        }
        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("from") || reader.name() == QLatin1String("to")) {
                section = Section::None;
            } else if (reader.name() == kind) {
                break;
            }
        }
    }
    return anchor;
}

// An anchor's on-sheet size: a twoCellAnchor spans the cells between its
// corners, a oneCellAnchor carries its own extent in EMU. Invalid when the
// anchor says neither, which leaves the object at its natural size.
QSize anchorSize(const Anchor &anchor, const Sheet &sheet)
{
    if (anchor.toCol >= 0 && anchor.toRow >= 0) {
        int width = 0;
        for (int col = anchor.fromCol; col < anchor.toCol; ++col) {
            width += int(sheet.columnWidth(col));
        }
        int height = 0;
        for (int row = anchor.fromRow; row < anchor.toRow; ++row) {
            height += int(sheet.rowHeight(row));
        }
        return {qMax(width, 1), qMax(height, 1)};
    }
    if (anchor.widthPx > 0 && anchor.heightPx > 0) {
        return {anchor.widthPx, anchor.heightPx};
    }
    return {};
}

// The first text inside c:title (rich runs concatenated).
QString readChartTitle(QXmlStreamReader &reader)
{
    QString title;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == QLatin1String("t")) {
            title += reader.readElementText();
        } else if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("title")) {
            break;
        }
    }
    return title;
}

// One <c:ser>: series name (c:tx plain or strRef), category and value refs
// (scatter spells them xVal/yVal).
ChartSeriesSpec readChartSeries(QXmlStreamReader &reader)
{
    ChartSeriesSpec series;
    QString nameRef;
    enum class Section { None, Tx, Cat, Val } section = Section::None;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("tx")) {
                section = Section::Tx;
            } else if (name == QLatin1String("cat") || name == QLatin1String("xVal")) {
                section = Section::Cat;
            } else if (name == QLatin1String("val") || name == QLatin1String("yVal")) {
                section = Section::Val;
            } else if (name == QLatin1String("v")) {
                if (section == Section::Tx && series.name.isEmpty()) {
                    series.name = reader.readElementText();
                } else {
                    reader.skipCurrentElement();
                }
            } else if (name == QLatin1String("f")) {
                const QString ref = reader.readElementText();
                if (section == Section::Tx) {
                    nameRef = ref;
                } else if (section == Section::Cat && series.catRange.isEmpty()) {
                    series.catRange = ref;
                } else if (section == Section::Val && series.valRange.isEmpty()) {
                    series.valRange = ref;
                }
            }
            continue;
        }
        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("ser")) {
                break;
            }
            if (reader.name() == QLatin1String("tx") || reader.name() == QLatin1String("cat")
                || reader.name() == QLatin1String("val") || reader.name() == QLatin1String("xVal")
                || reader.name() == QLatin1String("yVal")) {
                section = Section::None;
            }
        }
    }
    if (series.name.isEmpty()) {
        series.name = nameRef;
    }
    return series;
}

// One chart part: kind (first plot type; mixed charts read as their first —
// documented), grouping, title, series.
void readChartPart(const QByteArray &xml, ChartSpec *spec)
{
    QXmlStreamReader reader(xml);
    bool inPlotArea = false;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement) {
            if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("plotArea")) {
                inPlotArea = false;
            }
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("pivotSource") && !inPlotArea) { // a PivotChart (M74)
            while (reader.readNextStartElement()) {
                if (reader.name() == QLatin1String("name")) {
                    spec->pivotSource = reader.readElementText();
                } else {
                    reader.skipCurrentElement();
                }
            }
            continue;
        }
        if (name == QLatin1String("title")) {
            spec->title = readChartTitle(reader);
            continue;
        }
        if (name == QLatin1String("plotArea")) {
            inPlotArea = true;
            continue;
        }
        if (!inPlotArea) {
            continue;
        }
        if (name == QLatin1String("barChart")) {
            spec->kind = ChartSpec::Kind::Bar; // grouping below may upgrade
        } else if (name == QLatin1String("lineChart")) {
            spec->kind = ChartSpec::Kind::Line;
        } else if (name == QLatin1String("areaChart")) {
            spec->kind = ChartSpec::Kind::Area;
        } else if (name == QLatin1String("pieChart")) {
            spec->kind = ChartSpec::Kind::Pie;
        } else if (name == QLatin1String("scatterChart")) {
            spec->kind = ChartSpec::Kind::Scatter;
        } else if (name == QLatin1String("grouping")
                   && reader.attributes().value(QLatin1String("val")) == QLatin1String("stacked")
                   && spec->kind == ChartSpec::Kind::Bar) {
            spec->kind = ChartSpec::Kind::StackedBar;
        } else if (name == QLatin1String("ser")) {
            spec->series.append(readChartSeries(reader));
        }
    }
}

} // namespace

void readSheetDrawings(const QString &packagePath, Workbook *wb, qint32 sheetIndex)
{
    QXlsx::ZipReader zip(packagePath);
    if (!zip.exists()) {
        return;
    }
    const QVector<QString> parts = sheetPartMap(zip);
    if (sheetIndex < 0 || sheetIndex >= parts.size() || parts.at(sheetIndex).isEmpty()) {
        return;
    }
    const QString sheetPart = parts.at(sheetIndex);
    const int slash = sheetPart.lastIndexOf(QLatin1Char('/'));
    const QString ownerDir = sheetPart.left(slash);
    const QHash<QString, QPair<QString, QString>> sheetRels = readRels(zip, relsPathFor(sheetPart));
    QString drawingPart;
    for (auto it = sheetRels.constBegin(); it != sheetRels.constEnd(); ++it) {
        if (it.value().first.endsWith(QLatin1String("/drawing"))) {
            drawingPart = resolveTarget(ownerDir, it.value().second);
            break;
        }
    }
    if (drawingPart.isEmpty()) {
        return;
    }
    const QByteArray drawingXml = zip.fileData(drawingPart);
    if (drawingXml.isEmpty()) {
        return;
    }
    const QString drawingDir = drawingPart.left(drawingPart.lastIndexOf(QLatin1Char('/')));
    const QHash<QString, QPair<QString, QString>> drawingRels =
        readRels(zip, relsPathFor(drawingPart));
    const Sheet &sheet = wb->sheetAt(sheetIndex);

    // AGENT-NOTE (M66, D-075): only TOP-LEVEL anchors become charts and
    // pictures; an mc:AlternateContent (a slicer, a form control, a chart
    // only newer Excel draws, with its fallback picture) is carried whole
    // by the save (xlsx_carry_drawing.cpp), so nothing inside one is taken
    // in — its fallback picture would otherwise come back as a duplicate.
    // The positions of the objects taken in are recorded for the carry.
    QList<qint32> modelled;
    int depth = 0;
    int topIndex = -1;
    QXmlStreamReader reader(drawingXml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            --depth;
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        ++depth;
        if (depth == 2) {
            ++topIndex;
        }
        const QStringView name = reader.name();
        if (depth != 2 || (name != QLatin1String("twoCellAnchor") && name != QLatin1String("oneCellAnchor"))) {
            if (depth == 2) {
                reader.skipCurrentElement(); // AlternateContent, absoluteAnchor: carried
                --depth;
            }
            continue;
        }
        const QLatin1String kind = name == QLatin1String("twoCellAnchor")
            ? QLatin1String("twoCellAnchor")
            : QLatin1String("oneCellAnchor");
        const Anchor anchor = readAnchor(reader, kind);
        --depth; // readAnchor consumed the anchor's end tag
        QSize size = anchorSize(anchor, sheet);
        if (!anchor.picRid.isEmpty()) {
            const QString mediaPart =
                resolveTarget(drawingDir, drawingRels.value(anchor.picRid).second);
            const QByteArray bytes = zip.fileData(mediaPart);
            if (bytes.isEmpty()) {
                continue; // a blip pointing at nothing: not our data to invent
            }
            ImageSpec image;
            image.bytes = QSharedPointer<const QByteArray>::create(bytes);
            const int dot = mediaPart.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) {
                image.format = mediaPart.mid(dot + 1).toLower();
            }
            image.sheet = sheetIndex;
            image.anchorRow = anchor.fromRow;
            image.anchorCol = anchor.fromCol;
            image.widthPx = size.width();
            image.heightPx = size.height();
            wb->images.append(image);
            modelled.append(topIndex);
            continue;
        }
        const QString chartPart = resolveTarget(drawingDir, drawingRels.value(anchor.chartRid).second);
        const QByteArray chartXml = zip.fileData(chartPart);
        if (anchor.chartRid.isEmpty() || chartXml.isEmpty()) {
            continue;
        }
        ChartSpec spec;
        spec.sheet = sheetIndex;
        spec.anchorRow = anchor.fromRow;
        spec.anchorCol = anchor.fromCol;
        if (size.isValid()) {
            spec.widthPx = size.width();
            spec.heightPx = size.height();
        }
        readChartPart(chartXml, &spec);
        if (!spec.series.isEmpty()) {
            wb->charts.append(spec);
            // AGENT-GUARD (M74, D-083): a PivotChart is shown but stays out
            // of `modelled`, so the save carries its anchor and chart part
            // (c:pivotSource and all) instead of writing a plain chart; the
            // writer skips it (xlsx_write.cpp writeCharts).
            if (spec.pivotSource.isEmpty()) {
                modelled.append(topIndex);
            }
        }
    }
    SheetSource source = sheet.source();
    source.modelledObjects = modelled;
    wb->sheetAt(sheetIndex).setSource(source);
}

void readChartSheet(const QString &packagePath, Workbook *wb, qint32 sheetIndex, const QString &sheetPart)
{
    Sheet &sheet = wb->sheetAt(sheetIndex);
    sheet.setChartSheet(true);
    SheetSource source = sheet.source();
    source.part = sheetPart;
    sheet.setSource(source);
    QXlsx::ZipReader zip(packagePath);
    if (!zip.exists() || sheetPart.isEmpty()) {
        return;
    }
    const QString ownerDir = sheetPart.left(sheetPart.lastIndexOf(QLatin1Char('/')));
    QString drawingPart;
    for (const auto &[type, target] : readRels(zip, relsPathFor(sheetPart))) {
        if (type.endsWith(QLatin1String("/drawing"))) {
            drawingPart = resolveTarget(ownerDir, target);
            break;
        }
    }
    const QByteArray drawingXml = drawingPart.isEmpty() ? QByteArray() : zip.fileData(drawingPart);
    if (drawingXml.isEmpty()) {
        return;
    }
    // A chart sheet's drawing holds one absolute anchor with the chart.
    ChartSpec spec;
    spec.sheet = sheetIndex;
    QString chartRid;
    QXmlStreamReader reader(drawingXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == QLatin1String("ext") && spec.widthPx == ChartSpec{}.widthPx) {
            const int width = reader.attributes().value(QLatin1String("cx")).toInt() / 9525;
            const int height = reader.attributes().value(QLatin1String("cy")).toInt() / 9525;
            if (width > 0 && height > 0) {
                spec.widthPx = width;
                spec.heightPx = height;
            }
        } else if (reader.name() == QLatin1String("chart") && chartRid.isEmpty()) {
            chartRid = reader.attributes().value(QLatin1String("r:id")).toString();
        }
    }
    const QString drawingDir = drawingPart.left(drawingPart.lastIndexOf(QLatin1Char('/')));
    const QString chartPart = resolveTarget(drawingDir, readRels(zip, relsPathFor(drawingPart)).value(chartRid).second);
    const QByteArray chartXml = chartRid.isEmpty() ? QByteArray() : zip.fileData(chartPart);
    if (chartXml.isEmpty()) {
        return;
    }
    readChartPart(chartXml, &spec);
    wb->charts.append(spec);
}

} // namespace QindaOffice::Calc::IO
