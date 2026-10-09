// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A new slide size written into a package (internal to show_model; M75,
// D-084; pptx_design.h's resized()). In every slide, layout and master
// part, each top-level shape's position and size (a:xfrm / p:xfrm; a
// group's own box, not its child space) is scaled by `factor` about the
// slide's top-left and centred on the new slide; every text size, spacing,
// indent, tab, bullet size, inset, table grid column and row height and
// cell margin is scaled by it too — inside groups as well; the
// presentation's default text style and p:sldSz follow. A placeholder that
// spells no position keeps inheriting its layout's (scaled) one.

#include <QSizeF>

namespace QindaOffice::Ooxml {
class PackageWriter;
}

namespace QindaOffice::Show::Pptx {

void scalePackage(Ooxml::PackageWriter &package, const QSizeF &from, const QSizeF &to, qreal factor);

} // namespace QindaOffice::Show::Pptx
