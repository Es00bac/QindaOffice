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

    // Suffix appended to a settings file that could not be parsed. The bad
    // file is moved aside rather than overwritten, so nothing the user (or a
    // half-finished write) put there is ever destroyed.
    static constexpr const char *kQuarantineSuffix = ".corrupt";

private:
    // Moves an unparseable file to <path>.corrupt and reports it. Returns
    // false when even that fails, in which case save() refuses to run.
    [[nodiscard]] bool quarantine(const QString &reason);

    QString m_path;
    QVariantMap m_map;
    // False while an existing file could not be parsed AND could not be
    // moved aside; save() then refuses instead of replacing the file with a
    // map built from defaults.
    bool m_writable = true;
};

} // namespace QindaOffice::Core
