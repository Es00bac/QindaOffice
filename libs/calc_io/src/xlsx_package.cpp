// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_package.h"

#include <KZip>

#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace QindaOffice::Calc::IO {

namespace {

const QRegularExpression &targetPattern()
{
    static const QRegularExpression pattern(QStringLiteral("Target=\"(/[^\"]*)\""));
    return pattern;
}

// The directory of the part a .rels file describes: "xl/_rels/workbook.xml.rels"
// → "xl"; "_rels/.rels" → "" (the package root).
QString sourceDirOf(const QString &relsPath)
{
    const int rels = relsPath.lastIndexOf(QLatin1String("_rels/"));
    if (rels < 0) {
        return {};
    }
    QString dir = relsPath.left(rels);
    while (dir.endsWith(QLatin1Char('/'))) {
        dir.chop(1);
    }
    return dir;
}

QByteArray rewriteRels(const QByteArray &data, const QString &relsPath)
{
    QString text = QString::fromUtf8(data);
    const QString sourceDir = sourceDirOf(relsPath);
    QString out;
    qsizetype last = 0;
    auto it = targetPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += text.mid(last, match.capturedStart(0) - last);
        out += QStringLiteral("Target=\"%1\"").arg(relativeTarget(sourceDir, match.captured(1)));
        last = match.capturedEnd(0);
    }
    out += text.mid(last);
    return out.toUtf8();
}

void collect(const KArchiveDirectory *dir, const QString &prefix, QStringList &files)
{
    for (const QString &name : dir->entries()) {
        const KArchiveEntry *entry = dir->entry(name);
        const QString path = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
        if (entry->isDirectory()) {
            collect(static_cast<const KArchiveDirectory *>(entry), path, files);
        } else {
            files.append(path);
        }
    }
}

} // namespace

QString relativeTarget(const QString &sourceDir, const QString &target)
{
    if (!target.startsWith(QLatin1Char('/'))) {
        return target;
    }
    const QStringList targetParts = QDir::cleanPath(target.mid(1)).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList baseParts = sourceDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    qsizetype common = 0;
    while (common < baseParts.size() && common < targetParts.size() - 1
           && baseParts.at(common) == targetParts.at(common)) {
        ++common;
    }
    QStringList relative;
    for (qsizetype i = common; i < baseParts.size(); ++i) {
        relative.append(QStringLiteral(".."));
    }
    for (qsizetype i = common; i < targetParts.size(); ++i) {
        relative.append(targetParts.at(i));
    }
    return relative.join(QLatin1Char('/'));
}

bool xlsxPackageHasAbsoluteTargets(const QString &path)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return false;
    }
    QStringList files;
    collect(zip.directory(), QString(), files);
    for (const QString &file : files) {
        if (!file.endsWith(QLatin1String(".rels"))) {
            continue;
        }
        const auto *entry = static_cast<const KArchiveFile *>(zip.directory()->entry(file));
        if (entry != nullptr && targetPattern().match(QString::fromUtf8(entry->data())).hasMatch()) {
            return true;
        }
    }
    return false;
}

QString normalizedXlsxPackage(const QString &path, QTemporaryDir &scratch, QString *error)
{
    if (!xlsxPackageHasAbsoluteTargets(path)) {
        return path;
    }
    KZip in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read %1 as a zip package").arg(path);
        }
        return {};
    }
    if (!scratch.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("no scratch directory for %1").arg(path);
        }
        return {};
    }
    const QString outPath = scratch.filePath(QStringLiteral("normalized.xlsx"));
    KZip out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(outPath);
        }
        return {};
    }
    out.setCompression(KZip::DeflateCompression);
    QStringList files;
    collect(in.directory(), QString(), files);
    for (const QString &file : files) {
        const auto *entry = static_cast<const KArchiveFile *>(in.directory()->entry(file));
        if (entry == nullptr) {
            continue;
        }
        const QByteArray data = file.endsWith(QLatin1String(".rels")) ? rewriteRels(entry->data(), file)
                                                                      : entry->data();
        if (!out.writeFile(file, data)) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot write %1 into %2").arg(file, outPath);
            }
            return {};
        }
    }
    if (!out.close()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot finish %1").arg(outPath);
        }
        return {};
    }
    return outPath;
}

} // namespace QindaOffice::Calc::IO
