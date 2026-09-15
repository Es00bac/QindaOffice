// SPDX-License-Identifier: GPL-3.0-or-later
#include "recent_files.h"

#include "json_settings.h"

#include <QVariant>

namespace QindaOffice::Core {

RecentFiles::RecentFiles(QObject *parent)
    : QObject(parent)
{
}

bool RecentFiles::load(const QString &path, QString *error)
{
    m_path = path;
    JsonSettings settings(path);
    if (!settings.load(error)) {
        return false;
    }
    m_paths.clear();
    const QVariantList list = settings.value(QStringLiteral("recent")).toList();
    for (const QVariant &entry : list) {
        m_paths.append(entry.toString());
    }
    return true;
}

bool RecentFiles::save(QString *error) const
{
    if (m_path.isEmpty()) {
        return true;
    }
    JsonSettings settings(m_path);
    QVariantList list;
    list.reserve(m_paths.size());
    for (const QString &path : m_paths) {
        list.append(path);
    }
    settings.setValue(QStringLiteral("recent"), list);
    return settings.save(error);
}

void RecentFiles::add(const QString &canonicalPath)
{
    if (canonicalPath.isEmpty()) {
        return;
    }
    m_paths.removeAll(canonicalPath);
    m_paths.prepend(canonicalPath);
    while (m_paths.size() > kMaxEntries) {
        m_paths.removeLast();
    }
    save(nullptr);
    emit changed();
}

void RecentFiles::remove(const QString &canonicalPath)
{
    if (m_paths.removeAll(canonicalPath) > 0) {
        save(nullptr);
        emit changed();
    }
}

void RecentFiles::clear()
{
    m_paths.clear();
    save(nullptr);
    emit changed();
}

} // namespace QindaOffice::Core
