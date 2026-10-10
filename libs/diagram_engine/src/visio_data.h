// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio interchange: external data (M87, D-095) as Visio
// stores it — visio/data/recordsets.xml (DataRecordSets: a DataRecordSet
// per table with its DataColumns, PrimaryKey and RowMap rows linking rows
// to shapes) and a recordsetN.xml per table holding its rows as an ADO
// persisted rowset (an `_Visio_RowID_` column first). A linked shape's
// values are Shape Data rows named "_VisDM_<column>" with DataLinked=1.
//
// AGENT-NOTE (sources, D-095): part names, content types, relationship
// types, the DataRecordSet and rowset layouts and the _VisDM_ rows are as
// Visio 2013–2016 wrote them in public sample drawings; RowMap is from
// [MS-VSDX] only (no public sample has one). A recordset written here has
// no DataConnection: Visio shows its rows and the links but cannot refresh
// them until it is linked to its file again; QindaDiagram keeps the file
// name and refreshes it itself.
//
// AGENT-CONTRACT (with visio_write.cpp): while the recordsets and the links
// are as they were read, the package's own data parts travel verbatim (the
// relationship copier); any change rewrites them from the model.

#include "drawing.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace QindaOffice::Ooxml {
class PackageWriter;
}

namespace QindaOffice::Diagram::Visio {

struct ReadContext;
struct Source;

inline constexpr char16_t kRecordsetsRelType[] = u"http://schemas.microsoft.com/visio/2010/relationships/recordsets";
inline constexpr char16_t kRecordsetRelType[] = u"http://schemas.microsoft.com/visio/2010/relationships/recordset";
inline constexpr char16_t kRecordsetsType[] = u"application/vnd.ms-visio.recordsets+xml";
inline constexpr char16_t kRecordsetType[] = u"application/vnd.ms-visio.recordset+xml";

// "_VisDM_" + the column name, every run of characters other than letters
// and digits one "_" ("CPU (MHz)" → "_VisDM_CPU_MHz").
[[nodiscard]] QString linkedRowName(const QString &column);
// The columns a shape is linked by (its recordset's), or none.
[[nodiscard]] QStringList linkedColumns(const Drawing &drawing, const Shape &shape);

// The DataRecordSets of the package as model recordsets, and the shapes
// their RowMap rows link. Call once every page is read.
int readRecordsets(ReadContext &ctx, const QString &documentPart);

// Every shape's link (model shape id → link), the way the writer compares
// what it has with what was read.
[[nodiscard]] QHash<QString, DataLink> linksOf(const Drawing &drawing);
[[nodiscard]] bool recordsetsAsRead(const Drawing &drawing, const Source &source);

// recordsets.xml and a recordsetN.xml per recordset, from the model;
// `pageIds` (model page → Visio page ID) and `shapeIds` (model page →
// model shape → Visio ID) name the linked shapes. Nothing without data.
void writeRecordsets(Ooxml::PackageWriter &writer, const QString &documentPart, const Drawing &drawing,
                     const QHash<QString, QString> &pageIds, const QHash<QString, QHash<QString, int>> &shapeIds);

} // namespace QindaOffice::Diagram::Visio
