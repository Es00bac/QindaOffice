// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BooksDocument: a company file (`.qbooks`) as a Core::Document, so a
// QindaBooks window plugs into DocumentHost like every other app (rule 1:
// one company = one file = one window). Deliberately thin — the books are
// the CompanyFile; this class only maps the document lifecycle onto it.
//
// AGENT-CONTRACT (lifecycle, D-045; DocumentHost is the other side):
//  * Every posting commits to SQLite immediately, so an opened or created
//    company is NEVER dirty: there is nothing unsaved, Ctrl+W closes
//    without a Save/Discard question, and auto-save has nothing to do.
//  * Save (same path) is a successful no-op.
//  * Save As = "save a copy and switch to it": a VACUUM INTO copy at the
//    new path (replacing it only after the host's overwrite consent), the
//    window then works on the copy; the original stays as it was.
//  * The recovery journal is not used: SQLite's rollback journal already
//    makes every commit durable and every crash roll back to the last
//    commit. recoverySnapshot() is empty and loadRecovery() refuses.
//  * The one dirty state is an UNTITLED company (createUntitled): it lives
//    in a private scratch file until the first Save As gives it a path, so
//    closing it asks for consent and Discard deletes the scratch file.
//  * byteRevision is refreshed after every commit (CompanyFile::changed),
//    so the host's "changed on disk since load" check never mistakes our
//    own postings for another program's.

#include "company_file.h"
#include "document.h"

#include <QTemporaryDir>

#include <memory>

namespace QindaOffice::Books {

class BooksDocument : public Core::Document {
    Q_OBJECT
public:
    static constexpr const char *kMimeType = "application/x-qindabooks";
    static constexpr const char *kSuffix = "qbooks";

    explicit BooksDocument(QObject *parent = nullptr);
    ~BooksDocument() override;

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // A new company at `path` (the New Company flow picks the path first).
    bool createCompany(const QString &path, const CompanySettings &settings, const QString &chartTemplateId,
                       QString *error = nullptr);
    // A new company without a path yet, in a scratch file; dirty until
    // saved with Save As.
    bool createUntitled(const CompanySettings &settings, const QString &chartTemplateId, QString *error = nullptr);

    // The open books; null before load/create. Owned by the document and
    // replaced by Save As — never cache the pointer across companyChanged.
    [[nodiscard]] CompanyFile *company() const { return m_company.get(); }
    [[nodiscard]] bool isUntitled() const { return m_scratch != nullptr; }

signals:
    // The CompanyFile object was replaced (load, create, Save As).
    void companyChanged();
    // Forwarded CompanyFile::changed(): a mutation committed.
    void booksChanged();

private:
    void adopt(std::unique_ptr<CompanyFile> company, const QString &path, bool untitled);

    // AGENT-GUARD: declaration order = the scratch dir outlives the open
    // company (members are destroyed in reverse), so SQLite closes first.
    std::unique_ptr<QTemporaryDir> m_scratch;
    std::unique_ptr<CompanyFile> m_company;
};

} // namespace QindaOffice::Books
