// SPDX-License-Identifier: GPL-3.0-or-later
#include "atomic_file_store.h"

#include <QFileInfo>
#include <QSaveFile>

namespace QindaOffice::Core {

AtomicFileStore::Result AtomicFileStore::write(const QString &path, const QByteArray &data)
{
    Result result;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.write(data) != data.size()) {
        result.error = file.errorString();
        return result;
    }
    // commit() flushes, fsyncs and atomically replaces the target.
    if (!file.commit()) {
        result.error = file.errorString();
        return result;
    }
    result.ok = true;
    return result;
}

AtomicFileStore::Result AtomicFileStore::writeText(const QString &path, const QString &text)
{
    return write(path, text.toUtf8());
}

quint64 fileRevision(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return 0;
    }
    return static_cast<quint64>(info.size())
        ^ (static_cast<quint64>(info.lastModified().toMSecsSinceEpoch()) << 1);
}

} // namespace QindaOffice::Core
