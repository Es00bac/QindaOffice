// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Importers (M50, D-050):
//  * Org chart from CSV: one row per person with a name, a title and the
//    name of their manager (header names are matched loosely: "Name",
//    "Full name"; "Title", "Position", "Role"; "Manager", "Reports to",
//    "Supervisor"). Every other column becomes shape data. People become
//    org cards (the top of the tree an executive card), reporting lines
//    become connectors, and the tree layout arranges them.
//  * draw.io / diagrams.net: an UNCOMPRESSED .drawio (or .xml) file — each
//    <diagram> becomes a page; vertices map to the nearest stencil by
//    style (rectangles, rounded, ellipse, rhombus, triangle, hexagon,
//    parallelogram, cylinder, cloud, document, process, actor, swimlane,
//    note, text), with fill/stroke/font colours, dashes, rotation and
//    labels (HTML flattened to text); edges become connectors with their
//    source/target glue, style (orthogonal / curved / straight), arrows and
//    labels. Compressed diagrams (deflate + base64, the old default) are
//    refused with instructions; Visio .vsdx is out of scope.

#include "drawing.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaOffice::Diagram {

struct OrgChartResult {
    QStringList shapeIds;
    QStringList connectorIds;
    QStringList warnings;   // rows skipped, unknown managers
};

// Adds the org chart from `csv` to `page` (below anything already there).
bool importOrgChartCsv(Drawing &drawing, Page &page, const QByteArray &csv, OrgChartResult *result,
                       QString *error = nullptr);

// Parses CSV text into rows (RFC 4180: quotes, doubled quotes, CRLF).
[[nodiscard]] QVector<QStringList> parseCsv(const QString &text);

// Pages from a draw.io file; ids are fresh ids of `drawing` (the pages are
// not added — the caller decides where they go).
bool importDrawio(Drawing &drawing, const QByteArray &xml, QVector<Page> *pages, QStringList *warnings,
                  QString *error = nullptr);

} // namespace QindaOffice::Diagram
