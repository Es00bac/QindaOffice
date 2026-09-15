// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace QindaOffice::Core {
class RecoveryJournal;
class RecentFiles;
}

namespace QindaOffice::Shell {

class DocumentWindow;

// The seam between a DocumentWindow and its OfficeApplication (plan section
// 5.3's "app-facing seams" pattern): the window stays usable with a null
// implementation (tests, degraded runs) and the application keeps policy.
// All methods are GUI-thread only.
class AppServices {
public:
    virtual ~AppServices() = default;

    // May return nullptr (journal/recents disabled or app not wired).
    [[nodiscard]] virtual Core::RecoveryJournal *journal() = 0;
    [[nodiscard]] virtual Core::RecentFiles *recentFiles() = 0;

    // Canonical-path uniqueness across windows (ADR-0110).
    [[nodiscard]] virtual bool isPathOpenInAnotherWindow(const QString &canonicalPath,
                                                         const DocumentWindow *self) const = 0;

    // Journal key provisioning: fresh untitled key per window; path keys are
    // deterministic (SHA-256 of the canonical path).
    [[nodiscard]] virtual QString allocateUntitledJournalKey() = 0;
    [[nodiscard]] virtual QString journalKeyForPath(const QString &canonicalPath) const = 0;

    // Called after every successful save (recents bookkeeping).
    virtual void documentSaved(const QString &canonicalPath) = 0;

    // Restore-inventory policy (Settings menu toggle, default off).
    [[nodiscard]] virtual bool restorePreviousDocuments() const = 0;
    virtual void setRestorePreviousDocuments(bool enabled) = 0;
};

} // namespace QindaOffice::Shell
