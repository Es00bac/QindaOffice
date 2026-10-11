// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to office_math's property access (math_props.cpp,
// math_props_run.cpp): the helpers both halves share.

#include "math_props.h"

#include "ooxml_xml.h"

namespace QindaOffice::Math::detail {

[[nodiscard]] const QString &mathNamespace();
// OMML on/off values: "0", "false" and "off" are off, anything else on.
[[nodiscard]] bool onOffValue(const QString &v);
[[nodiscard]] Justification parseJc(const QString &v, Justification fallback);
[[nodiscard]] LimitLocation parseLimLoc(const QString &v, LimitLocation fallback);
// The m:val of `parent`'s math child `name` ("" when absent).
[[nodiscard]] QString childVal(const Ooxml::XmlElement &parent, QStringView name);

} // namespace QindaOffice::Math::detail
