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

// The same for an external target (a hyperlink's URL, an external
// workbook): TargetMode="External", the target escaped as an attribute
// value (URLs carry '&'). M66, D-075.
[[nodiscard]] QString appendExternalRelationship(QByteArray *relsXml, const QString &type,
                                                 const QString &target);

// `text` escaped for an XML attribute value (& < > " and control
// characters XML 1.0 cannot carry dropped).
[[nodiscard]] QByteArray xmlAttribute(const QString &text);

// Adds a <Override PartName ContentType> to [Content_Types].xml. No-op when
// the override already exists.
void appendContentTypeOverride(QByteArray *contentTypesXml, const QString &partName,
                               const QString &contentType);

// Adds a <Default Extension ContentType> (media and VML parts are declared
// by extension). No-op when the extension is already declared.
void appendContentTypeDefault(QByteArray *contentTypesXml, const QString &extension,
                              const QString &contentType);

// Inserts `fragment` into a worksheet part directly before the FIRST of the
// `beforeTags` anchors found (schema order, e.g. autoFilter goes before
// mergeCells). Returns false when no anchor exists — the caller then fails
// the save loudly. M78 (D-087): an anchor is a CHILD of the root element
// ("extLst"; "<extLst" is accepted too) or the root's end tag
// ("/worksheet", "</worksheet>"); an element of the same name deeper down —
// a conditional format's own <extLst> — never counts.
[[nodiscard]] bool insertBeforeAny(QByteArray *sheetXml, const QStringList &beforeTags,
                                   const QByteArray &fragment);

// Inserts `fragment` directly after the closing `</tag>` of the root's
// child `tag` (schema order when the predecessor is unconditional —
// autoFilter follows </sheetData>). Returns false when the tag is absent.
[[nodiscard]] bool insertAfterTag(QByteArray *sheetXml, const QString &tag,
                                  const QByteArray &fragment);

// M78 (D-087): where the first of `tags` starts among the root element's
// children (or the root's end tag, "/worksheet"), or -1.
[[nodiscard]] qsizetype topLevelTagOffset(const QByteArray &xml, const QStringList &tags);
// Where the end tag (</name>) of the root's child `name` starts, or -1
// (a self-closing child has none).
[[nodiscard]] qsizetype topLevelElementEnd(const QByteArray &xml, const QString &name);

} // namespace QindaOffice::Calc::IO
