// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace QindaOffice::Core {

// Paths-only restore inventory (ADR-0110/ADR-0065 format, plan section 2.7):
// {"version":1,"enabled":false,"paths":[≤32],"activeIndex":n}, ≤64 KiB on
// disk, content/history never persisted. DEFAULT OFF; the Settings menu
// toggle flips enabled and persists through the same file.
class RestoreInventory {
public:
    static constexpr int kMaxPaths = 32;

    struct Snapshot {
        QStringList paths;
        int activeIndex = 0;
    };

    explicit RestoreInventory(QString path);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    [[nodiscard]] Snapshot snapshot() const { return m_snapshot; }
    void setSnapshot(const Snapshot &snapshot);

    [[nodiscard]] bool load(QString *error = nullptr);
    [[nodiscard]] bool persist(QString *error = nullptr) const;

private:
    QString m_path;
    Snapshot m_snapshot;
    bool m_enabled = false;
};

} // namespace QindaOffice::Core
