// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QRegularExpression>
#include <QSaveFile>

namespace QindaOffice::Apps::Notes::NotebookStorage {

// AGENT-GUARD: ids come from notebook.json and are interpolated into file
// paths. A hostile notebook with "../.." in an id must never escape the
// notebook directory; reject anything but the safe identifier shape.
inline bool idIsSafe(const QString &id)
{
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return safe.match(id).hasMatch();
}

// Atomic write: the earlier release truncate-wrote notebook.json and page
// payloads, so a crash or full disk mid-save destroyed the previous content.
inline bool writeJsonAtomically(const QString &path, const QByteArray &payload, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

