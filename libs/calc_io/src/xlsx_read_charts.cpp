// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package reader for chart parts (xlsx_read_xml.h): QXlsx's chart
// support is write-only (no public series accessor), so chart XML is parsed
// here — plot type and grouping, title, per-series tx/cat/val references,
// and the drawing anchor that places the chart on the sheet. Producers may
// put charts on any path (the sample uses xl/drawings/charts/), so parts
// are always resolved through the rels, never by convention.

#include "xlsx_read_xml.h"

#include "xlsx_parts.h"

#include "workbook.h"

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

void readSheetCharts(const QString &packagePath, Workbook *wb, qint32 sheetIndex)
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

    QXmlStreamReader reader(drawingXml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name != QLatin1String("twoCellAnchor") && name != QLatin1String("oneCellAnchor")) {
            continue;
        }
        const QLatin1String kind = name == QLatin1String("twoCellAnchor")
            ? QLatin1String("twoCellAnchor")
            : QLatin1String("oneCellAnchor");
        const Anchor anchor = readAnchor(reader, kind);
        const QString chartPart = resolveTarget(drawingDir, drawingRels.value(anchor.chartRid).second);
        const QByteArray chartXml = zip.fileData(chartPart);
        if (anchor.chartRid.isEmpty() || chartXml.isEmpty()) {
            continue;
        }
        ChartSpec spec;
        spec.sheet = sheetIndex;
        spec.anchorRow = anchor.fromRow;
        spec.anchorCol = anchor.fromCol;
        if (anchor.toCol >= 0 && anchor.toRow >= 0) {
            int width = 0;
            for (int col = anchor.fromCol; col < anchor.toCol; ++col) {
                width += int(sheet.columnWidth(col));
            }
            int height = 0;
            for (int row = anchor.fromRow; row < anchor.toRow; ++row) {
                height += int(sheet.rowHeight(row));
            }
            spec.widthPx = qMax(width, 1);
            spec.heightPx = qMax(height, 1);
        } else if (anchor.widthPx > 0 && anchor.heightPx > 0) {
            spec.widthPx = anchor.widthPx;
            spec.heightPx = anchor.heightPx;
        }
        readChartPart(chartXml, &spec);
        if (!spec.series.isEmpty()) {
            wb->charts.append(spec);
        }
    }
}

} // namespace QindaOffice::Calc::IO
