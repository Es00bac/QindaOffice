// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared DrawingML rendering boundary for Word (D-101). The adapters own
// their results and never mutate the producer's XML. Geometry is in points.
#include "presentation.h"
#include "ooxml_package.h"
#include "ooxml_theme.h"
namespace QindaOffice::Show::DrawingIO {
[[nodiscard]] Shape wordShape(const Ooxml::XmlElement &wsp,
                              const std::shared_ptr<const Ooxml::Package> &package,
                              const QString &part, const Ooxml::Theme &theme);
[[nodiscard]] QList<Shape> diagram(const Ooxml::XmlElement &drawing,
                                  const std::shared_ptr<const Ooxml::Package> &package,
                                  const QString &part, const Ooxml::Theme &theme);
} // namespace QindaOffice::Show::DrawingIO
