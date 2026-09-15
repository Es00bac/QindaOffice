// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

namespace QindaOffice::Core {

// Most-recently-used document paths (plan section 5.2, bound ≤20). Stored as
// a flat JSON array via JsonSettings in the per-app config dir; mutations
// persist immediately so the list survives crashes.
class RecentFiles final : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 20;

    explicit RecentFiles(QObject *parent = nullptr);

    [[nodiscard]] bool load(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool save(QString *error = nullptr) const;

    void add(const QString &canonicalPath); // move-to-front dedupe, bounded
    void remove(const QString &canonicalPath);
    void clear();

    [[nodiscard]] QStringList paths() const { return m_paths; }
    [[nodiscard]] QString storagePath() const { return m_path; }

signals:
    void changed();

private:
    QStringList m_paths;
    QString m_path;
};

} // namespace QindaOffice::Core
