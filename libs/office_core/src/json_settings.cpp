// SPDX-License-Identifier: GPL-3.0-or-later
#include "json_settings.h"

#include "atomic_file_store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

namespace QindaOffice::Core {

JsonSettings::JsonSettings(const QString &path)
    : m_path(path)
{
    load(nullptr);
}

QVariant JsonSettings::value(const QString &key, const QVariant &fallback) const
{
    return m_map.value(key, fallback);
}

void JsonSettings::setValue(const QString &key, const QVariant &value)
{
    m_map.insert(key, value);
}

bool JsonSettings::load(QString *error)
{
    QFile file(m_path);
    if (!file.exists()) {
        return true; // pristine settings are valid
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr) {
            *error = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("settings file is not a JSON object");
        }
        return false;
    }
    m_map = doc.object().toVariantMap();
    return true;
}

bool JsonSettings::save(QString *error) const
{
    const QJsonDocument doc(QJsonObject::fromVariantMap(m_map));
    const AtomicFileStore::Result result =
        AtomicFileStore::write(m_path, doc.toJson(QJsonDocument::Compact));
    if (!result.ok && error != nullptr) {
        *error = result.error;
    }
    return result.ok;
}

} // namespace QindaOffice::Core
