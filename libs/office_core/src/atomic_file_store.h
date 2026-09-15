// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

namespace QindaOffice::Core {

// Atomic same-directory writes (QSaveFile: flush + fsync + atomic replace),
// keeping ADR-0022's "text documents stay local and atomic" guarantee for
// every format the suite saves.
class AtomicFileStore {
public:
    struct Result {
        bool ok = false;
        QString error;
    };

    [[nodiscard]] static Result write(const QString &path, const QByteArray &data);
    [[nodiscard]] static Result writeText(const QString &path, const QString &text);
};

// Content-independent on-disk revision (size + mtime) used for the
// optimistic overwrite check: Document records the revision at load; save
// refuses to clobber a file whose revision moved meanwhile unless the user
// consents. Returns 0 when the file does not exist (i.e. "created since
// load" - the common untitled-save case, which always passes the check
// against a fresh save-as target... callers compare only when a previous
// revision was non-zero and the path already existed at load time).
[[nodiscard]] quint64 fileRevision(const QString &path);

} // namespace QindaOffice::Core
