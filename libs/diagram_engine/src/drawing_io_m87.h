// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the .qdiagram file (drawing_io.cpp): the members M87 (D-095)
// added — layers, hyperlinks, comments, data links and recordsets,
// container behaviour, background pages, line-jump style, Auto Size and
// print setup.
//
// AGENT-CONTRACT (with drawing_io.cpp and older readers): every member is
// written ONLY when it differs from its default, so a drawing that uses
// none of them keeps the exact JSON M50/M65 wrote, and an older QindaDiagram
// keeps them as unknown members (`extra`) and writes them back. The *Keys()
// lists name every member read here, so the callers leave them out of
// `extra`.

#include "drawing.h"

#include <QJsonObject>

namespace QindaOffice::Diagram::FileM87 {

void shapeToJson(const Shape &shape, QJsonObject &object);
void shapeFromJson(const QJsonObject &object, Shape &shape);
void connectorToJson(const Connector &connector, QJsonObject &object);
void connectorFromJson(const QJsonObject &object, Connector &connector);
void pageToJson(const Page &page, QJsonObject &object);
void pageFromJson(const QJsonObject &object, Page &page);
void drawingToJson(const Drawing &drawing, QJsonObject &object);
void drawingFromJson(const QJsonObject &object, Drawing &drawing);

// Removes the members read above from `extra`.
void stripShapeKeys(QJsonObject &extra);
void stripConnectorKeys(QJsonObject &extra);
void stripPageKeys(QJsonObject &extra);
void stripDrawingKeys(QJsonObject &extra);

} // namespace QindaOffice::Diagram::FileM87
