// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio writer: what M87 (D-095) writes — layers, line
// jumps, print setup and Auto Size in the PageSheet; an item's layers and
// hyperlinks; Visio's container cells and the Relationships between
// containers and their members; the comments part. The cells and their
// values are the ones visio_read_m87.cpp reads (its AGENT-NOTE).
//
// AGENT-CONTRACT (with visio_read_m87.cpp): what is written here reads back
// as the same model values; a cell is written in Visio's spelling (layer
// rows indexed from 0 with all eleven cells, hyperlink rows named Row_n,
// DEPENDSON(1, …) on a container and DEPENDSON(4, …) on a member).

#include "visio_sheet.h"
#include "visio_xml.h"

#include "drawing.h"

#include <QHash>
#include <QList>

namespace QindaOffice::Diagram::Visio {

void writeLayerMember(XmlElement &shape, const QList<int> &layers);
// Replaces the Hyperlink section; rows `master` lends that the list does
// not overwrite are deleted (Del="1").
void writeHyperlinks(XmlElement &shape, const QList<Hyperlink> &links, const Sheet *master = nullptr);
// User.msvStructureType and friends for a container; nothing otherwise.
void writeContainerCells(XmlElement &shape, const Shape &container, double pxPerInch);
// The Relationships cell from the Visio IDs of the shape's members (it is a
// container) and of the containers it is in; removed when both are empty.
void writeRelationships(XmlElement &shape, const QList<int> &members, const QList<int> &containers);
// The page's layers, line jumps, print setup and Auto Size into its
// PageSheet: everything (`read` null: a new page) or what differs from
// what was read.
void writePageSettings(XmlElement &pageSheet, const Page &page, const Page *read);
[[nodiscard]] bool pageSettingsDiffer(const Page &page, const Page &read);
// The comments part: every page's comments, `pageIds` (model page id →
// Visio page ID) and `shapeIds` (model page id → model item id → Visio
// ID) naming where they are. Authors are numbered from 1 in order met.
[[nodiscard]] XmlElementPtr commentsElement(const Drawing &drawing, const QHash<QString, QString> &pageIds,
                                            const QHash<QString, QHash<QString, int>> &shapeIds);

inline constexpr char16_t kCommentsRelType[] = u"http://schemas.microsoft.com/visio/2010/relationships/comments";
inline constexpr char16_t kCommentsType[] = u"application/vnd.ms-visio.comments+xml";

} // namespace QindaOffice::Diagram::Visio
