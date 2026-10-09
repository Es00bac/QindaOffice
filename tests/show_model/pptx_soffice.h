// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Headless LibreOffice for the pptx gates (M75): whether it runs here, a
// file converted into a directory, the pages of a PDF. Tests QSKIP their
// LibreOffice step when soffice is not available.

#include "support/soffice_env.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace PptxFixture {

inline bool sofficeAvailable()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    return probe.waitForFinished(15000) && probe.exitCode() == 0;
}

// `bytes` written to `dir`/`file` and converted to `format`; the converted
// file's bytes (empty on failure, with soffice's complaint in `error`).
inline QByteArray convertWithSoffice(const QTemporaryDir &dir, const QString &file, const QByteArray &bytes,
                                     const QString &format, QString *error = nullptr)
{
    const QString path = dir.filePath(file);
    const QString out = dir.filePath(QFileInfo(file).completeBaseName() + QLatin1Char('-') + format);
    QDir().mkpath(out);
    QFile input(path);
    if (!input.open(QIODevice::WriteOnly) || input.write(bytes) != bytes.size()) {
        return {};
    }
    input.close();
    QProcess soffice;
    QindaOffice::Test::startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                                              QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                                              QStringLiteral("--convert-to"), format, QStringLiteral("--outdir"), out, path});
    soffice.waitForFinished(180000);
    QFile converted(out + QLatin1Char('/') + QFileInfo(file).completeBaseName() + QLatin1Char('.') + format.section(QLatin1Char(':'), 0, 0));
    if (!converted.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QString::fromUtf8(soffice.readAllStandardError());
        }
        return {};
    }
    return converted.readAll();
}

inline int pdfPages(const QByteArray &pdf)
{
    static const QRegularExpression kPage(QStringLiteral("/Type\\s*/Page[^s]"));
    int pages = 0;
    for (auto it = kPage.globalMatch(QString::fromLatin1(pdf)); it.hasNext(); it.next()) {
        ++pages;
    }
    return pages;
}

} // namespace PptxFixture
