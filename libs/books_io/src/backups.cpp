// SPDX-License-Identifier: GPL-3.0-or-later
#include "backups.h"

#include "company_file.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <tuple>

namespace QindaOffice::Books {

namespace {

QString baseName(const CompanyFile &file)
{
    return QFileInfo(file.path()).completeBaseName();
}

} // namespace

QStringList Backups::list(const CompanyFile &file, const QString &dir)
{
    // AGENT-NOTE: the timestamp in the name sorts lexically = by age, so
    // pruning never depends on file mtimes (which copies and syncs change).
    // Same-second copies get "-2", "-3" …; they sort by that number, not
    // lexically ("-2" < ".qbooks" would put them before the first copy).
    const QRegularExpression pattern(QStringLiteral("^%1-(\\d{8}-\\d{6})(?:-(\\d+))?\\.qbooks$")
                                         .arg(QRegularExpression::escape(baseName(file))));
    QList<std::tuple<QString, int, QString>> found;
    const QStringList names = QDir(dir).entryList({QStringLiteral("*.qbooks")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        const QRegularExpressionMatch m = pattern.match(name);
        if (m.hasMatch()) {
            found << std::make_tuple(m.captured(1), m.captured(2).isEmpty() ? 1 : m.captured(2).toInt(),
                                     QDir(dir).filePath(name));
        }
    }
    std::sort(found.begin(), found.end());
    QStringList out;
    for (const auto &entry : std::as_const(found)) {
        out << std::get<2>(entry);
    }
    return out;
}

Result Backups::createTimestamped(CompanyFile &file, const QString &dir, int keep, QString *createdPath)
{
    if (!QDir().mkpath(dir)) {
        return Result::failure(ErrorCode::Storage, QStringLiteral("cannot create %1").arg(dir));
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString target = QDir(dir).filePath(QStringLiteral("%1-%2.qbooks").arg(baseName(file), stamp));
    for (int n = 2; QFileInfo::exists(target); ++n) {
        target = QDir(dir).filePath(QStringLiteral("%1-%2-%3.qbooks").arg(baseName(file), stamp).arg(n));
    }
    const Result copied = file.backupTo(target);
    if (!copied.ok) {
        return copied;
    }
    if (createdPath != nullptr) {
        *createdPath = target;
    }
    QStringList existing = list(file, dir);
    while (keep > 0 && existing.size() > keep) {
        QFile::remove(existing.takeFirst());
    }
    return Result::success();
}

} // namespace QindaOffice::Books
