// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to plan_fields.cpp and plan_field_edit.cpp.

#include "plan_types.h"

namespace QindaOffice::Plan {

// The constraint's name as the sheet shows it ("Start No Earlier Than").
inline QString constraintName(ConstraintType type)
{
    static const QString names[] = {
        QStringLiteral("As Soon As Possible"),    QStringLiteral("As Late As Possible"),
        QStringLiteral("Must Start On"),          QStringLiteral("Must Finish On"),
        QStringLiteral("Start No Earlier Than"),  QStringLiteral("Start No Later Than"),
        QStringLiteral("Finish No Earlier Than"), QStringLiteral("Finish No Later Than"),
    };
    return names[int(type)];
}

} // namespace QindaOffice::Plan
