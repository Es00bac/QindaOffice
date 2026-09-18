// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DesktopSettings (plan section 7.5): read-only access to the QindaQt
// desktop's org.qindaqt.Settings1 service for app-relevant accessibility
// values. The wire contract is a bounded generic scoped snapshot:
// GetSnapshot(QStringList keys) -> QVariantMap with a "values" map.
// Any failure (no session bus, service absent, malformed reply) degrades
// to the documented default — apps must never hard-depend on the desktop.

#include <QStringList>
#include <QVariant>

namespace QindaOffice::Quick {

class DesktopSettings {
public:
    // Synchronous snapshot of the requested keys; missing/failed keys are
    // simply absent from the returned map.
    [[nodiscard]] static QVariantMap values(const QStringList &keys);

    // accessibility.reducedMotion; false when the desktop cannot answer.
    [[nodiscard]] static bool reducedMotion();
};

} // namespace QindaOffice::Quick
