// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QTimer;

namespace QindaOffice::Core {

// Crash-recovery journal (plan section 2.7): one JSON envelope per document
// key under the per-app state dir, written atomically, cleared on save and
// on discard. The payload is the document's native recoverySnapshot();
// oversized documents are skipped, never truncated.
//
// Envelope: {"version":1,"key":"...","path":"/abs/file"|"","untitled":bool,
//            "savedAt":"<iso>","payload":"<base64>"}
//
// Debounce: schedule() restarts a per-key timer (default 3 s after edits);
// tests shrink it via setDebounceInterval().
class RecoveryJournal final : public QObject {
    Q_OBJECT
public:
    // Plan bound: ≤4 MiB text / ≤16 MiB binary per snapshot.
    static constexpr qint64 kMaxTextPayload = 4 * 1024 * 1024;
    static constexpr qint64 kMaxBinaryPayload = 16 * 1024 * 1024;

    explicit RecoveryJournal(QString dir, QObject *parent = nullptr);
    ~RecoveryJournal() override;

    void setDebounceInterval(int milliseconds) { m_debounceMs = milliseconds; }

    // Stable key for a canonical path (SHA-256 hex); empty path is invalid —
    // untitled documents use allocateUntitledKey().
    [[nodiscard]] static QString keyForPath(const QString &canonicalPath);
    [[nodiscard]] QString allocateUntitledKey();

    // Debounced write: the payload is captured when the timer fires, so a
    // burst of edits collapses into one write of the latest state.
    void schedule(const QString &key, const QString &path, bool untitled,
                  std::function<QByteArray()> payloadProvider,
                  qint64 maxPayloadBytes = kMaxTextPayload);
    void writeNow(const QString &key, const QString &path, bool untitled,
                  const QByteArray &payload, qint64 maxPayloadBytes = kMaxTextPayload);

    void clear(const QString &key);
    // The user dismissed this entry: it leaves the journal for good (never
    // offered again), but its envelope is MOVED to <dir>-discarded/, which
    // keeps the newest kDiscardedKeep, so a mistaken dismissal can still be
    // dug out by hand. Nothing in the suite reads that folder.
    void discard(const QString &key);
    static constexpr int kDiscardedKeep = 20;
    [[nodiscard]] QString discardedDir() const { return m_dir + QStringLiteral("-discarded"); }
    [[nodiscard]] QStringList pendingKeys() const;

    struct Entry {
        QString key;
        QString path;
        bool untitled = false;
        QByteArray payload;
        QDateTime savedAt; // invalid when the envelope has none
    };
    [[nodiscard]] std::optional<Entry> entry(const QString &key) const;

signals:
    // A snapshot over its bound was not written (never truncated): the
    // window says so, because otherwise a crash would find no recovery
    // point — or an older one — for a document that looked protected.
    void snapshotSkipped(const QString &key, qint64 bytes);

private:
    struct Pending {
        QString path;
        bool untitled = false;
        std::function<QByteArray()> provider;
        qint64 maxPayloadBytes = kMaxTextPayload;
        QTimer *timer = nullptr;
    };

    void fire(const QString &key);
    [[nodiscard]] QString fileFor(const QString &key) const;

    QHash<QString, Pending> m_pending;
    QString m_dir;
    int m_debounceMs = 3000;
    quint64 m_untitledSeq = 0;
};

} // namespace QindaOffice::Core
