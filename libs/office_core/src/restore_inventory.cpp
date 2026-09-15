// SPDX-License-Identifier: GPL-3.0-or-later
#include "restore_inventory.h"

#include "atomic_file_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Core {

RestoreInventory::RestoreInventory(QString path)
    : m_path(std::move(path))
{
    load(nullptr);
}

void RestoreInventory::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void RestoreInventory::setSnapshot(const Snapshot &snapshot)
{
    m_snapshot.paths = snapshot.paths.mid(0, kMaxPaths);
    m_snapshot.activeIndex =
        qBound(0, snapshot.activeIndex, qMax(0, m_snapshot.paths.size() - 1));
}

bool RestoreInventory::load(QString *error)
{
    QFile file(m_path);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.size() > 64 * 1024) {
        if (error != nullptr) {
            *error = QStringLiteral("restore inventory exceeds its 64 KiB bound");
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("restore inventory is not a JSON object");
        }
        return false;
    }
    const QJsonObject object = doc.object();
    m_enabled = object.value(QStringLiteral("enabled")).toBool(false);
    Snapshot snapshot;
    const QJsonArray array = object.value(QStringLiteral("paths")).toArray();
    for (const QJsonValue &value : array) {
        snapshot.paths.append(value.toString());
    }
    snapshot.activeIndex = object.value(QStringLiteral("activeIndex")).toInt(0);
    setSnapshot(snapshot);
    return true;
}

bool RestoreInventory::persist(QString *error) const
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("enabled"), m_enabled);
    QJsonArray array;
    for (const QString &path : m_snapshot.paths) {
        array.append(path);
    }
    object.insert(QStringLiteral("paths"), array);
    object.insert(QStringLiteral("activeIndex"), m_snapshot.activeIndex);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (json.size() > 64 * 1024) {
        if (error != nullptr) {
            *error = QStringLiteral("restore inventory exceeds its 64 KiB bound");
        }
        return false;
    }
    const AtomicFileStore::Result result =
        AtomicFileStore::write(m_path, json + '\n');
    if (!result.ok && error != nullptr) {
        *error = result.error;
    }
    return result.ok;
}

} // namespace QindaOffice::Core
