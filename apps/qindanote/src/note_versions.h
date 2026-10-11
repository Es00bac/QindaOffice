// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ink_persistence.h"

#include <QDateTime>
#include <QList>

namespace QindaOffice::Apps::Notes::Versions {

struct Entry {
    QString id;
    QString title;
    QDateTime captured;
    qint64 bytes = 0;
};

// Synchronous, notebook-owned history; callers serialize with page writes.
// No background thread or external service. Failed captures leave the current
// page untouched. Paths contain validated section/page/version identifiers.
inline constexpr int kLimit = 32;
inline constexpr qint64 kByteLimit = 256LL * 1024 * 1024;
inline constexpr qint64 kSnapshotLimit = 64LL * 1024 * 1024;
inline constexpr int kIntervalSeconds = 300;

[[nodiscard]] QList<Entry> list(const QString &notebook, const QString &section,
                               const QString &page, QString *error = nullptr);
[[nodiscard]] bool capture(const QString &notebook, const QString &section,
                           const Ink::PageEnvelope &page, bool force,
                           QString *error = nullptr,
                           QDateTime now = QDateTime::currentDateTimeUtc());
[[nodiscard]] bool read(const QString &notebook, const QString &section,
                        const QString &page, const QString &version,
                        Ink::PageEnvelope *result, QString *error = nullptr);
[[nodiscard]] bool remove(const QString &notebook, const QString &section,
                          const QString &page, const QString &version,
                          QString *error = nullptr);
// Copies retained snapshots and assets on Save As. Never removes source data.
[[nodiscard]] bool copyTree(const QString &source, const QString &target, QString *error = nullptr);

} // namespace QindaOffice::Apps::Notes::Versions
