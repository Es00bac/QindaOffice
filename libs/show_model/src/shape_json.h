// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shape <-> JSON codec (M13): the clipboard representation for QindaShow
// shapes. Lives in the model (QtCore only) so the editor, tests and any
// future scripting share one canonical round trip. The codec is total:
// every Shape field survives, including the image payload (base64) and
// the extension namespace.

#include "presentation.h"

class QJsonObject;

namespace QindaOffice::Show {

[[nodiscard]] QJsonObject shapeToJson(const Shape &shape);
[[nodiscard]] Shape shapeFromJson(const QJsonObject &object);
[[nodiscard]] QByteArray shapesToJson(const QList<Shape> &shapes);
[[nodiscard]] QList<Shape> shapesFromJson(const QByteArray &payload);

} // namespace QindaOffice::Show
