// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File fields (M65, D-074): a picture or any other document kept inside the
// .qbase file, one per field per record.
//
// AGENT-CONTRACT (storage and reads — base_types.h has the type row; the
// datasheet, forms, exports and the Access import all rely on it):
//  * A File column is declared FILE and holds the file's bytes as a BLOB
//    (NULL = no file), at most kFileFieldLimit bytes. Only the bytes are
//    kept: a saved copy is named after its record (suggestedFileName).
//  * Reads for display never load the bytes. Records::read/readRow select
//    length(column) — which SQLite answers without reading overflow pages —
//    and read the first kFileSummaryHead bytes through incremental blob I/O
//    (Connection::blobHead), giving the TEXT summary
//    "<size>:<hex of the first bytes>" (fileSummary). fileInfo() — and so
//    displayText/editText — turns it into "PNG image · 24 KB". The bytes
//    are read on demand (Records::readFile) and written only through
//    Records::setFile (one captured Change).
//  * The kind comes from the bytes' signature, never from a name.

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <optional>

namespace QindaOffice::Base {

constexpr qint64 kFileFieldLimit = qint64(32) * 1024 * 1024;
constexpr int kFileSummaryHead = 32;

struct FileInfo {
    qint64 size = 0;
    QString kind;       // "PNG image", "PDF document", "File"
    QString extension;  // "png", "pdf", "bin"
    QString mimeType;   // "image/png"
    bool image = false; // a raster or vector picture format (the app checks Qt can decode it)
};

// The kind of a file from its first bytes.
[[nodiscard]] FileInfo sniffFile(const QByteArray &head, qint64 size);
// The summary text a read gives a File cell.
[[nodiscard]] QString fileSummary(qint64 size, const QByteArray &head);
// A stored File value — the summary text a read gives, or the bytes — as
// FileInfo; nullopt for NULL or text that is neither.
[[nodiscard]] std::optional<FileInfo> fileInfo(const QVariant &stored);
// "PNG image · 24 KB" (the datasheet's text for a File cell).
[[nodiscard]] QString fileDisplayText(const FileInfo &info);
// "1.2 MB", "24 KB", "512 bytes".
[[nodiscard]] QString formatByteSize(qint64 bytes);
// A name for a saved copy: "Members-12-Photo.png" (characters a file name
// cannot hold become "_").
[[nodiscard]] QString suggestedFileName(const QString &table, qint64 rowid, const QString &field,
                                        const FileInfo &info);

} // namespace QindaOffice::Base
