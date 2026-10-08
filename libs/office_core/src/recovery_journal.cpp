// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_journal.h"

#include "atomic_file_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace QindaOffice::Core {

RecoveryJournal::RecoveryJournal(QString dir, QObject *parent)
    : QObject(parent)
    , m_dir(std::move(dir))
{
    QDir().mkpath(m_dir);
}

RecoveryJournal::~RecoveryJournal()
{
    // Flush pending writes so a quit-with-edits does not lose the journal
    // entry; payload providers must still be valid during destruction.
    const QStringList keys = m_pending.keys();
    for (const QString &key : keys) {
        fire(key);
    }
}

QString RecoveryJournal::keyForPath(const QString &canonicalPath)
{
    if (canonicalPath.isEmpty()) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(canonicalPath.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString RecoveryJournal::allocateUntitledKey()
{
    ++m_untitledSeq;
    return QStringLiteral("untitled-%1-%2").arg(QCoreApplication::applicationPid())
        .arg(m_untitledSeq);
}

void RecoveryJournal::schedule(const QString &key, const QString &path, bool untitled,
                               std::function<QByteArray()> payloadProvider,
                               qint64 maxPayloadBytes)
{
    if (key.isEmpty()) {
        return;
    }
    auto it = m_pending.find(key);
    if (it == m_pending.end()) {
        Pending pending;
        pending.path = path;
        pending.untitled = untitled;
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, key] { fire(key); });
        pending.timer = timer;
        it = m_pending.insert(key, std::move(pending));
    }
    it->path = path;
    it->untitled = untitled;
    it->provider = std::move(payloadProvider);
    it->maxPayloadBytes = maxPayloadBytes;
    it->timer->start(m_debounceMs);
}

void RecoveryJournal::fire(const QString &key)
{
    const auto it = m_pending.constFind(key);
    if (it == m_pending.constEnd()) {
        return;
    }
    const QByteArray payload = it->provider ? it->provider() : QByteArray();
    const qint64 maxBytes = it->maxPayloadBytes;
    writeNow(key, it->path, it->untitled, payload, maxBytes);
    it->timer->deleteLater();
    m_pending.erase(it);
}

void RecoveryJournal::writeNow(const QString &key, const QString &path, bool untitled,
                               const QByteArray &payload, qint64 maxPayloadBytes)
{
    if (key.isEmpty()) {
        return;
    }
    // AGENT-GUARD: oversized documents are skipped, never truncated (plan
    // section 2.7) - a partial snapshot would restore corrupt content.
    if (payload.size() > maxPayloadBytes) {
        qWarning().noquote() << "recovery journal: skipping oversized snapshot for"
                             << (path.isEmpty() ? key : path);
        emit snapshotSkipped(key, payload.size());
        return;
    }
    // AGENT-GUARD: an EMPTY snapshot is not a recovery point. Engines whose
    // recoverySnapshot() is still a stub (QindaCalc, QindaNote) return {},
    // and writing those envelopes made every launch offer "Recover unsaved
    // changes to X?" for an entry whose restore always fails — and the
    // failed restore never cleared it, so the prompt returned forever.
    // Drop any stale file for the key so the nag cannot survive a stub.
    if (payload.isEmpty()) {
        QFile::remove(fileFor(key));
        return;
    }
    QJsonObject envelope;
    envelope.insert(QStringLiteral("version"), 1);
    envelope.insert(QStringLiteral("key"), key);
    envelope.insert(QStringLiteral("path"), path);
    envelope.insert(QStringLiteral("untitled"), untitled);
    envelope.insert(QStringLiteral("savedAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    envelope.insert(QStringLiteral("payload"), QString::fromLatin1(payload.toBase64()));

    const AtomicFileStore::Result result =
        AtomicFileStore::write(fileFor(key),
                               QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    if (!result.ok) {
        qWarning().noquote() << "recovery journal: write failed for" << key << result.error;
    }
}

void RecoveryJournal::clear(const QString &key)
{
    const auto it = m_pending.find(key);
    if (it != m_pending.end()) {
        it->timer->deleteLater();
        m_pending.erase(it);
    }
    QFile::remove(fileFor(key));
}

void RecoveryJournal::discard(const QString &key)
{
    const auto it = m_pending.find(key);
    if (it != m_pending.end()) {
        it->timer->deleteLater();
        m_pending.erase(it);
    }
    const QString source = fileFor(key);
    if (!QFile::exists(source)) {
        return;
    }
    QDir().mkpath(discardedDir());
    const QString target = discardedDir() + QLatin1Char('/') + key + QStringLiteral(".json");
    QFile::remove(target);
    if (!QFile::rename(source, target)) {
        // AGENT-GUARD: a dismissal must stick even when the backup cannot
        // be written — the entry is removed either way.
        QFile::remove(source);
    }
    QDir dir(discardedDir());
    const QFileInfoList kept = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files,
                                                 QDir::Time); // newest first
    for (qsizetype i = kDiscardedKeep; i < kept.size(); ++i) {
        QFile::remove(kept.at(i).absoluteFilePath());
    }
}

QStringList RecoveryJournal::pendingKeys() const
{
    const QStringList files = QDir(m_dir).entryList({QStringLiteral("*.json")},
                                                    QDir::Files, QDir::Name);
    QStringList keys;
    keys.reserve(files.size());
    for (const QString &file : files) {
        keys.append(file.chopped(QStringLiteral(".json").size()));
    }
    return keys;
}

std::optional<RecoveryJournal::Entry> RecoveryJournal::entry(const QString &key) const
{
    QFile file(fileFor(key));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = doc.object();
    Entry result;
    result.key = object.value(QStringLiteral("key")).toString();
    result.path = object.value(QStringLiteral("path")).toString();
    result.untitled = object.value(QStringLiteral("untitled")).toBool(false);
    result.payload = QByteArray::fromBase64(
        object.value(QStringLiteral("payload")).toString().toLatin1());
    result.savedAt = QDateTime::fromString(object.value(QStringLiteral("savedAt")).toString(),
                                           Qt::ISODate);
    return result;
}

QString RecoveryJournal::fileFor(const QString &key) const
{
    // Keys are hex digests or untitled-<pid>-<seq>; both filename-safe.
    return m_dir + QLatin1Char('/') + key + QStringLiteral(".json");
}

} // namespace QindaOffice::Core
