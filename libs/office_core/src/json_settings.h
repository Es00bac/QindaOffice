// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QVariantMap>

class QString;

namespace QindaOffice::Core {

// One atomic JSON settings file per app under the config dir (plan section
// 2.9: "the suite's own settings live in $XDG_CONFIG_HOME/qindaoffice/
// <app>.json (atomic JSON)"). Read-modify-write through valueMap(); save()
// is atomic via QSaveFile.
class JsonSettings {
public:
    explicit JsonSettings(const QString &path);

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QVariantMap valueMap() const { return m_map; }
    [[nodiscard]] QVariant value(const QString &key, const QVariant &fallback = {}) const;

    void setValue(const QString &key, const QVariant &value);

    [[nodiscard]] bool load(QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;

private:
    QString m_path;
    QVariantMap m_map;
};

} // namespace QindaOffice::Core
