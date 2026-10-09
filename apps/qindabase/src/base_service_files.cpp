// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: file fields — what a cell holds, its preview, and Insert /
// Save / Remove (M65, D-074; base_file_field.h).
#include "base_csv.h"
#include "base_document.h"
#include "base_file_field.h"
#include "base_records.h"
#include "base_service.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// A picture format this Qt build decodes (the preview), by extension.
bool decodable(const QString &extension)
{
    static const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    return formats.contains(extension.toLatin1());
}

} // namespace

QVariantMap BaseService::fileInfo(const QString &table, qint64 rowid, const QString &field) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<QByteArray> bytes = Engine::Records::readFile(*f, table, rowid, field, &error);
    if (!bytes) {
        return failed(error);
    }
    if (bytes->isEmpty()) {
        return {{QStringLiteral("ok"), true}, {QStringLiteral("hasFile"), false}};
    }
    const Engine::FileInfo info = Engine::sniffFile(bytes->left(Engine::kFileSummaryHead), bytes->size());
    QVariantMap out{{QStringLiteral("ok"), true},
                    {QStringLiteral("hasFile"), true},
                    {QStringLiteral("kind"), info.kind},
                    {QStringLiteral("label"), Engine::fileDisplayText(info)},
                    {QStringLiteral("size"), bytes->size()},
                    {QStringLiteral("sizeText"), Engine::formatByteSize(bytes->size())},
                    {QStringLiteral("image"), false},
                    {QStringLiteral("suggestedName"), Engine::suggestedFileName(table, rowid, field, info)}};
    if (info.image && decodable(info.extension)) {
        // AGENT-NOTE: named by content — Tk.Thumbnail caches by URL, so a
        // replaced picture must arrive under a new one.
        const QString name = QStringLiteral("preview-%1.%2")
                                 .arg(QString::fromLatin1(QCryptographicHash::hash(*bytes, QCryptographicHash::Sha1)
                                                              .toHex()
                                                              .left(20)),
                                      info.extension);
        const QString path = scratchPath(name);
        if (QFileInfo::exists(path) || Engine::writeFileAtomically(path, *bytes).ok) {
            out.insert(QStringLiteral("image"), true);
            out.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(path));
        }
    }
    return out;
}

QVariantMap BaseService::insertFile(const QString &table, qint64 rowid, const QString &field, const QUrl &url)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QFile source(localPath(url));
    if (!source.open(QIODevice::ReadOnly)) {
        return failed(source.errorString());
    }
    if (source.size() > Engine::kFileFieldLimit) {
        return failed(tr("%1 is %2; a file field holds files up to %3.")
                          .arg(QFileInfo(source).fileName(), Engine::formatByteSize(source.size()),
                               Engine::formatByteSize(Engine::kFileFieldLimit)));
    }
    const QByteArray bytes = source.readAll();
    if (bytes.isEmpty()) {
        return failed(tr("%1 is empty.").arg(QFileInfo(source).fileName()));
    }
    Engine::Change change;
    const Engine::Result r = Engine::Records::setFile(*f, table, rowid, field, bytes, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap BaseService::removeFile(const QString &table, qint64 rowid, const QString &field)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    Engine::Change change;
    const Engine::Result r = Engine::Records::setFile(*f, table, rowid, field, QByteArray(), &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap BaseService::saveFile(const QString &table, qint64 rowid, const QString &field, const QUrl &url) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<QByteArray> bytes = Engine::Records::readFile(*f, table, rowid, field, &error);
    if (!bytes) {
        return failed(error);
    }
    if (bytes->isEmpty()) {
        return failed(tr("%1 holds no file.").arg(field));
    }
    const QString path = localPath(url);
    const Engine::Result r = Engine::writeFileAtomically(path, *bytes);
    if (!r.ok) {
        return failed(r.error);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

} // namespace QindaOffice::Apps::Base
