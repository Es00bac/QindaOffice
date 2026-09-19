// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// XML helpers shared by the package generators and the post-pass: rels and
// content-types editing, and schema-ordered fragment insertion into a sheet
// part. The post-pass only ever rewrites packages QXlsx itself produced, so
// elements are unprefixed and part naming is deterministic — every helper
// fails loudly when its anchor is missing rather than producing a silently
// broken package (D-012 logic).

#include <QByteArray>
#include <QList>
#include <QString>

namespace QindaOffice::Calc::IO {

// Appends a relationship of `type` pointing at `target` to a .rels part
// (created when empty). Returns the rId used (always fresh: existing ids are
// collected first because producers are free to use non-numeric ids).
[[nodiscard]] QString appendRelationship(QByteArray *relsXml, const QString &type,
                                         const QString &target);

// Adds a <Override PartName ContentType> to [Content_Types].xml. No-op when
// the override already exists.
void appendContentTypeOverride(QByteArray *contentTypesXml, const QString &partName,
                               const QString &contentType);

// Inserts `fragment` into a worksheet part directly before the FIRST of the
// `beforeTags` anchors found (schema order, e.g. autoFilter goes before
// mergeCells). Returns false when no anchor exists — the caller then fails
// the save loudly.
[[nodiscard]] bool insertBeforeAny(QByteArray *sheetXml, const QStringList &beforeTags,
                                   const QByteArray &fragment);

// Inserts `fragment` directly after the closing `</tag>` (schema order when
// the predecessor is unconditional — autoFilter follows </sheetData>).
// Returns false when the tag is absent.
[[nodiscard]] bool insertAfterTag(QByteArray *sheetXml, const QString &tag,
                                  const QByteArray &fragment);

} // namespace QindaOffice::Calc::IO
