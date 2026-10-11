// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_versions.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>

namespace QindaOffice::Apps::Notes::Versions {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool safe(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,96}$"));
    return pattern.match(id).hasMatch();
}

QString folder(const QString &notebook, const QString &section, const QString &page, QString *error)
{
    if (notebook.isEmpty() || !safe(section) || !safe(page)) {
        fail(error, QStringLiteral("Save the notebook before keeping page versions."));
        return {};
    }
    QString path = QDir(notebook).absolutePath();
    for (const QString &part : {QStringLiteral("sections"), section, QStringLiteral("versions"), page}) {
        path = QDir(path).filePath(part);
        if (QFileInfo(path).isSymLink()) {
            fail(error, QStringLiteral("A page-version directory must not be a symbolic link."));
            return {};
        }
    }
    return path;
}

bool write(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, file.errorString());
    return true;
}

QJsonObject snapshot(const QString &path, QString *error)
{
    QFile file(path);
    if (QFileInfo(path).isSymLink() || file.size() > kSnapshotLimit || !file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("Cannot read a bounded page version."));
        return {};
    }
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    const QJsonObject value = json.object();
    if (!json.isObject() || value.value(QStringLiteral("version")).toInt() != 1
        || !value.value(QStringLiteral("page")).isObject()
        || !QDateTime::fromString(value.value(QStringLiteral("captured")).toString(), Qt::ISODateWithMs).isValid()) {
        fail(error, QStringLiteral("The page version is damaged or uses an unknown format."));
        return {};
    }
    return value;
}

QByteArray digest(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
} // namespace

QList<Entry> list(const QString &notebook, const QString &section, const QString &page, QString *error)
{
    QList<Entry> result;
    const QString dir = folder(notebook, section, page, error);
    if (dir.isEmpty()) return result;
    // Newest first; each file is complete and independently recoverable.
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &file : files) {
        if (result.size() >= kLimit) break;
        const QJsonObject data = snapshot(file.absoluteFilePath(), error);
        if (data.isEmpty()) continue;
        if (data.value(QStringLiteral("page")).toObject().value(QStringLiteral("id")).toString() != page) {
            fail(error, QStringLiteral("A retained version belongs to a different page."));
            continue;
        }
        result.append({file.completeBaseName(), data.value(QStringLiteral("page")).toObject().value(QStringLiteral("title")).toString(),
                       QDateTime::fromString(data.value(QStringLiteral("captured")).toString(), Qt::ISODateWithMs),
                       file.size()});
    }
    return result;
}

bool read(const QString &notebook, const QString &section, const QString &page, const QString &version,
          Ink::PageEnvelope *result, QString *error)
{
    const QString dir = folder(notebook, section, page, error);
    if (dir.isEmpty() || !safe(version) || !result) return fail(error, QStringLiteral("Invalid page version."));
    const QJsonObject data = snapshot(QDir(dir).filePath(version + QStringLiteral(".json")), error);
    Ink::PageEnvelope decoded;
    if (data.isEmpty()) return false;
    const QByteArray raw = QJsonDocument(data.value(QStringLiteral("page")).toObject()).toJson(QJsonDocument::Compact);
    if (digest(raw) != data.value(QStringLiteral("sha256")).toString().toLatin1())
        return fail(error, QStringLiteral("The page version failed its integrity check."));
    if (!Ink::pageFromJson(QJsonDocument(data.value(QStringLiteral("page")).toObject()).toJson(), &decoded)
        || decoded.id != page) return fail(error, QStringLiteral("The version does not contain this page."));
    *result = std::move(decoded);
    return true;
}

bool capture(const QString &notebook, const QString &section, const Ink::PageEnvelope &page, bool force,
             QString *error, QDateTime now)
{
    const QString dir = folder(notebook, section, page.id, error);
    if (dir.isEmpty()) return false;
    if (!now.isValid()) return fail(error, QStringLiteral("Invalid version time."));
    const QByteArray raw = Ink::pageToJson(page);
    const QByteArray hash = digest(raw);
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed);
    if (!files.isEmpty()) {
        // Autosave must not deserialize up to 256 MiB of historical pages
        // every half second. The immutable filename carries time + full hash.
        const QString latest = files.first().completeBaseName();
        if (latest.endsWith(QLatin1Char('-') + QString::fromLatin1(hash))) {
            if (!force) return true;
            // Restore relies on its forced capture being readable. Do not
            // trust a matching filename after a retained file was damaged.
            const QJsonObject saved = snapshot(files.first().absoluteFilePath(), nullptr);
            const QByteArray kept = QJsonDocument(saved.value(QStringLiteral("page")).toObject()).toJson(QJsonDocument::Compact);
            if (!saved.isEmpty() && digest(kept) == hash && saved.value(QStringLiteral("sha256")).toString().toLatin1() == hash)
                return true;
        }
        QDateTime captured = QDateTime::fromString(latest.left(18), QStringLiteral("yyyyMMddTHHmmsszzz"));
        captured.setTimeZone(QTimeZone::UTC);
        if (captured.isValid()) {
            if (!force && captured.secsTo(now) < kIntervalSeconds) return true;
            if (now <= captured) now = captured.addMSecs(1);
        }
    }
    const QByteArray data = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("captured"), now.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("title"), page.title}, {QStringLiteral("sha256"), QString::fromLatin1(hash)},
        {QStringLiteral("page"), QJsonDocument::fromJson(raw).object()}}).toJson(QJsonDocument::Compact);
    if (data.size() > kSnapshotLimit) return fail(error, QStringLiteral("A page version exceeds the 64 MiB limit."));
    if (!QDir().mkpath(dir)) return fail(error, QStringLiteral("Cannot create the page-version directory."));
    const QString id = now.toUTC().toString(QStringLiteral("yyyyMMddTHHmmsszzz")) + QLatin1Char('-') + QString::fromLatin1(hash);
    if (!write(QDir(dir).filePath(id + QStringLiteral(".json")), data, error)) return false;
    qint64 total = 0;
    int count = 0;
    for (const QFileInfo &file : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed)) {
        total += file.size();
        if (++count > kLimit || total > kByteLimit) {
            if (!QFile::remove(file.absoluteFilePath())) return fail(error, QStringLiteral("Cannot prune an old page version."));
        }
    }
    return true;
}

bool remove(const QString &notebook, const QString &section, const QString &page, const QString &version, QString *error)
{
    const QString dir = folder(notebook, section, page, error);
    if (dir.isEmpty() || !safe(version)) return fail(error, QStringLiteral("Invalid page version."));
    return QFile::remove(QDir(dir).filePath(version + QStringLiteral(".json")))
        || fail(error, QStringLiteral("Cannot delete the selected page version."));
}

bool copyTree(const QString &source, const QString &target, QString *error)
{
    const QDir dir(source);
    if (!dir.exists()) return true;
    if (QFileInfo(source).isSymLink()) return fail(error, QStringLiteral("Notebook data must not be a symbolic link."));
    const QString from = dir.absolutePath();
    const QString to = QDir(target).absolutePath();
    if (from == to) return true;
    if (to.startsWith(from + QLatin1Char('/')) || QFileInfo(target).isSymLink())
        return fail(error, QStringLiteral("The notebook copy cannot be inside its own data."));
    if (!QDir().mkpath(target)) return fail(error, QStringLiteral("Cannot create a notebook copy directory."));
    for (const QFileInfo &item : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (item.isSymLink()) return fail(error, QStringLiteral("Notebook assets and versions must not contain symbolic links."));
        const QString destination = QDir(target).filePath(item.fileName());
        if (item.isDir()) {
            if (!copyTree(item.absoluteFilePath(), destination, error)) return false;
        } else {
            QFile input(item.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly)) return fail(error, input.errorString());
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly)) return fail(error, output.errorString());
            while (!input.atEnd()) {
                const QByteArray chunk = input.read(1024 * 1024);
                if (chunk.isEmpty() || output.write(chunk) != chunk.size()) return fail(error, QStringLiteral("Cannot copy notebook data."));
            }
            if (!output.commit()) return fail(error, output.errorString());
        }
    }
    return true;
}
} // namespace QindaOffice::Apps::Notes::Versions
