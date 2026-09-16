// SPDX-License-Identifier: GPL-3.0-or-later
#include "json_settings.h"

#include "atomic_file_store.h"

#include <QDebug>
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
        const QString reason = parseError.error != QJsonParseError::NoError
            ? parseError.errorString()
            : QStringLiteral("settings file is not a JSON object");
        if (error != nullptr) {
            *error = reason;
        }
        // AGENT-GUARD: never silently overwrite an unparseable settings file.
        // load() left m_map empty and returned false, and the next setValue()
        // + save() then replaced the whole file with that one key — every
        // other setting in it was destroyed without a word. Move the bad file
        // aside instead: the user keeps it, and the app keeps working.
        file.close();
        m_writable = quarantine(reason);
        return false;
    }
    m_map = doc.object().toVariantMap();
    return true;
}

bool JsonSettings::quarantine(const QString &reason)
{
    const QString aside = m_path + QLatin1String(kQuarantineSuffix);
    QFile::remove(aside); // only ever an older quarantine of the same file
    if (!QFile::rename(m_path, aside)) {
        qWarning().noquote() << "settings:" << m_path << "is unreadable (" << reason
                             << ") and could not be moved aside; refusing to overwrite it";
        return false;
    }
    qWarning().noquote() << "settings:" << m_path << "was unreadable (" << reason
                         << "); moved to" << aside << "and starting from defaults";
    return true;
}

bool JsonSettings::save(QString *error) const
{
    if (!m_writable) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is unreadable and could not be moved aside")
                         .arg(m_path);
        }
        return false;
    }
    const QJsonDocument doc(QJsonObject::fromVariantMap(m_map));
    const AtomicFileStore::Result result =
        AtomicFileStore::write(m_path, doc.toJson(QJsonDocument::Compact));
    if (!result.ok && error != nullptr) {
        *error = result.error;
    }
    return result.ok;
}

} // namespace QindaOffice::Core
