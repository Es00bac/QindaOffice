// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native Word graphics (M93, D-101). Image formats carry immutable content
// snapshots so QTextDocument's normal undo/redo, cloning and recovery work.
// A fresh resource URL for each rendered state prevents an undo from
// showing a later edit's pixels. All operations are GUI-thread only.
#include "docx_model.h"
#include "docx_properties.h"
#include "ooxml_copier.h"
#include "ooxml_theme.h"
#include <QImage>
#include <QTextImageFormat>

namespace QindaOffice::Docx {
// Reserved 160–164 for M93; math uses its own object handler and the
// existing kInlineXml properties. Never reuse these offsets.
inline constexpr int kGraphicKind = kDocxBase + 160; // "chart", "shape", "smartArt"
inline constexpr int kGraphicData = kDocxBase + 161; // QVariantMap current content
inline constexpr int kGraphicSnapshot = kDocxBase + 162; // QVariantMap imported content
inline constexpr int kGraphicSource = kDocxBase + 163; // chart/data source part

// False leaves unsupported markup on the reader's preserved-object path.
bool readGraphic(const Ooxml::XmlElement &frame, const std::shared_ptr<const Ooxml::Package> &source,
                 const QString &story, const Ooxml::Theme &theme, QTextImageFormat &format, QImage &image);
// A new graphic, with default data. The caller inserts it as one undo step.
[[nodiscard]] QTextImageFormat newGraphic(const QString &kind, QTextDocument &document);
// User-facing editor values: chart data as tab-separated rows (first row
// series names); shape text/preset/fill; diagram one text per line.
[[nodiscard]] QVariantMap graphicInfo(const QTextImageFormat &format);
// Validates fully before changing format or resources; false sets error.
bool editGraphic(QTextImageFormat &format, const QVariantMap &answer, QTextDocument &document, QString *error);
void refreshGraphic(QTextImageFormat &format, QTextDocument &document);
// An edited/new graphic's entire run, relationships already carried.
// Null means unchanged: the writer must carry kInlineXml normally.
[[nodiscard]] Ooxml::XmlElementPtr writeGraphic(const QTextCharFormat &format, const WordParts &parts,
                                               Ooxml::PackageWriter &writer, const QString &story, QString *error);
} // namespace QindaOffice::Docx
