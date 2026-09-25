// SPDX-License-Identifier: GPL-3.0-or-later
#include "books_document.h"

#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

namespace QindaOffice::Books {

BooksDocument::BooksDocument(QObject *parent)
    : Core::Document(parent)
{
}

BooksDocument::~BooksDocument() = default;

void BooksDocument::adopt(std::unique_ptr<CompanyFile> company, const QString &path, bool untitled)
{
    m_company = std::move(company);
    connect(m_company.get(), &CompanyFile::changed, this, [this] {
        refreshByteRevision();
        emit booksChanged();
    });
    setCanonicalPath(untitled ? QString() : path);
    refreshByteRevision();
    setDirty(untitled);
    emit companyChanged();
}

bool BooksDocument::load(const QString &path, QString *error)
{
    std::unique_ptr<CompanyFile> company = CompanyFile::open(path, error);
    if (!company) {
        return false;
    }
    m_scratch.reset();
    adopt(std::move(company), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool BooksDocument::createCompany(const QString &path, const CompanySettings &settings,
                                  const QString &chartTemplateId, QString *error)
{
    std::unique_ptr<CompanyFile> company = CompanyFile::create(path, settings, chartTemplateId, error);
    if (!company) {
        return false;
    }
    m_scratch.reset();
    adopt(std::move(company), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool BooksDocument::createUntitled(const CompanySettings &settings, const QString &chartTemplateId, QString *error)
{
    auto scratch = std::make_unique<QTemporaryDir>();
    if (!scratch->isValid()) {
        if (error != nullptr) {
            *error = scratch->errorString();
        }
        return false;
    }
    std::unique_ptr<CompanyFile> company =
        CompanyFile::create(scratch->filePath(QStringLiteral("untitled.qbooks")), settings, chartTemplateId, error);
    if (!company) {
        return false;
    }
    m_company.reset(); // close before the old scratch dir goes away
    m_scratch = std::move(scratch);
    adopt(std::move(company), QString(), true);
    return true;
}

bool BooksDocument::save(const QString &path, QString *error)
{
    if (!m_company) {
        if (error != nullptr) {
            *error = tr("No company file is open.");
        }
        return false;
    }
    const QString target = QFileInfo(path).absoluteFilePath();
    if (!isUntitled() && target == QFileInfo(m_company->path()).absoluteFilePath()) {
        // Everything is committed already: Save is a no-op that succeeds.
        refreshByteRevision();
        setDirty(false);
        return true;
    }
    // Save As: a consistent copy next to the target, then an atomic rename
    // over it (the host has already asked before replacing a file).
    const QString temp = target + QStringLiteral(".saving");
    QFile::remove(temp);
    const Result copied = m_company->backupTo(temp);
    if (!copied.ok) {
        if (error != nullptr) {
            *error = copied.error;
        }
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(temp.toStdString(), target.toStdString(), ec);
    if (ec) {
        QFile::remove(temp);
        if (error != nullptr) {
            *error = QString::fromStdString(ec.message());
        }
        return false;
    }
    std::unique_ptr<CompanyFile> reopened = CompanyFile::open(target, error);
    if (!reopened) {
        return false;
    }
    reopened->setUserName(m_company->userName());
    m_company.reset();
    m_scratch.reset(); // an untitled company's scratch file goes with it
    adopt(std::move(reopened), target, false);
    return true;
}

QStringList BooksDocument::mimeTypes() const
{
    return {QString::fromLatin1(kMimeType)};
}

QByteArray BooksDocument::recoverySnapshot() const
{
    return {};
}

bool BooksDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(snapshot);
    if (error != nullptr) {
        *error = tr("Company files are saved as you work; there is nothing to recover.");
    }
    return false;
}

} // namespace QindaOffice::Books
