// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The .qdiagram file (M50, D-050): UTF-8 JSON, `"format": "qindadiagram"`,
// `"version": 1`. Pictures are base64 inside the file, so a drawing is one
// self-contained file. Saves are atomic (QSaveFile: a crash mid-save leaves
// the previous file). A file from a NEWER major version is refused rather
// than half-read; members this version does not know are kept (`extra`)
// and written back.
//
// The same shape/connector JSON is the clipboard fragment
// (application/x-qindadiagram-fragment) copy/paste uses between windows.

#include "drawing.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace QindaOffice::Diagram {

inline constexpr int kFormatVersion = 1;
inline constexpr char kFragmentMime[] = "application/x-qindadiagram-fragment";
inline constexpr char kFileMime[] = "application/x-qindadiagram";

[[nodiscard]] QByteArray drawingToJson(const Drawing &drawing);
bool drawingFromJson(const QByteArray &bytes, Drawing *drawing, QString *error = nullptr);
bool saveDrawing(const QString &path, const Drawing &drawing, QString *error = nullptr);
bool loadDrawing(const QString &path, Drawing *drawing, QString *error = nullptr);

[[nodiscard]] QJsonObject shapeToJson(const Shape &shape);
[[nodiscard]] Shape shapeFromJson(const QJsonObject &object);
[[nodiscard]] QJsonObject connectorToJson(const Connector &connector);
[[nodiscard]] Connector connectorFromJson(const QJsonObject &object);
[[nodiscard]] QJsonObject pageToJson(const Page &page);
[[nodiscard]] Page pageFromJson(const QJsonObject &object);

// The items `ids` of `page` (connectors between them included when both
// ends' shapes are listed, or when listed themselves) as a fragment.
[[nodiscard]] QByteArray fragmentOf(const Page &page, const QStringList &ids);
// Parses a fragment; false on anything that is not one.
bool parseFragment(const QByteArray &bytes, QVector<Shape> *shapes, QVector<Connector> *connectors,
                   QString *error = nullptr);

} // namespace QindaOffice::Diagram
