// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_journal.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

class RecoveryJournalTest : public QObject {
    Q_OBJECT
private slots:
    void pathKeysAreStableAndDistinct();
    void untitledKeysAreUnique();
    void writeNowEntryRoundTrip();
    void clearRemovesEntry();
    void debounceCoalescesAndCapturesLatest();
    void oversizedPayloadIsSkippedNotTruncated();
    void emptySnapshotIsNotARecoveryPoint();
};

void RecoveryJournalTest::pathKeysAreStableAndDistinct()
{
    QCOMPARE(RecoveryJournal::keyForPath(QStringLiteral("/a.txt")),
             RecoveryJournal::keyForPath(QStringLiteral("/a.txt")));
    QVERIFY(RecoveryJournal::keyForPath(QStringLiteral("/a.txt"))
            != RecoveryJournal::keyForPath(QStringLiteral("/b.txt")));
    QVERIFY(RecoveryJournal::keyForPath(QString()).isEmpty());
}

void RecoveryJournalTest::untitledKeysAreUnique()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    const QString first = journal.allocateUntitledKey();
    const QString second = journal.allocateUntitledKey();
    QVERIFY(first.startsWith(QStringLiteral("untitled-")));
    QVERIFY(first != second);
}

void RecoveryJournalTest::writeNowEntryRoundTrip()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    const QString key = RecoveryJournal::keyForPath(QStringLiteral("/doc.txt"));
    journal.writeNow(key, QStringLiteral("/doc.txt"), false,
                     QByteArray("unsaved contents"));
    QCOMPARE(journal.pendingKeys(), QStringList{key});
    const auto entry = journal.entry(key);
    QVERIFY(entry.has_value());
    QCOMPARE(entry->path, QStringLiteral("/doc.txt"));
    QVERIFY(!entry->untitled);
    QCOMPARE(entry->payload, QByteArray("unsaved contents"));
}

void RecoveryJournalTest::clearRemovesEntry()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    const QString key = RecoveryJournal::keyForPath(QStringLiteral("/doc.txt"));
    journal.writeNow(key, QStringLiteral("/doc.txt"), false, QByteArray("x"));
    QVERIFY(!journal.pendingKeys().isEmpty());
    journal.clear(key);
    QVERIFY(journal.pendingKeys().isEmpty());
    QVERIFY(!journal.entry(key).has_value());
}

void RecoveryJournalTest::debounceCoalescesAndCapturesLatest()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    journal.setDebounceInterval(40);
    const QString key = QStringLiteral("untitled-test-1");
    QString latest = QStringLiteral("v1");
    journal.schedule(key, QString(), true, [&latest] { return latest.toUtf8(); });
    latest = QStringLiteral("v2");
    journal.schedule(key, QString(), true, [&latest] { return latest.toUtf8(); });
    // Provider captured at fire time: the latest value wins after one wait.
    QTRY_VERIFY_WITH_TIMEOUT(!journal.pendingKeys().isEmpty(), 2000);
    const auto entry = journal.entry(key);
    QVERIFY(entry.has_value());
    QCOMPARE(entry->payload, QByteArray("v2"));
}

void RecoveryJournalTest::oversizedPayloadIsSkippedNotTruncated()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    const QString key = RecoveryJournal::keyForPath(QStringLiteral("/big.txt"));
    const QByteArray huge(RecoveryJournal::kMaxTextPayload + 1, 'x');
    journal.writeNow(key, QStringLiteral("/big.txt"), false, huge);
    QVERIFY(journal.pendingKeys().isEmpty()); // skipped entirely
}

// Engines whose recoverySnapshot() is still a stub return {}. Writing those
// envelopes produced a "Recover unsaved changes to X?" prompt whose restore
// always failed, and the failed restore never cleared the entry — so the
// prompt came back on every launch, forever.
void RecoveryJournalTest::emptySnapshotIsNotARecoveryPoint()
{
    QTemporaryDir dir;
    RecoveryJournal journal(dir.path());
    const QString key = RecoveryJournal::keyForPath(QStringLiteral("/stub.xlsx"));
    journal.writeNow(key, QStringLiteral("/stub.xlsx"), false, QByteArray());
    QVERIFY(journal.pendingKeys().isEmpty());

    // An empty snapshot also retires an entry a previous non-empty write left
    // behind, so a stub engine cannot keep a stale prompt alive.
    journal.writeNow(key, QStringLiteral("/stub.xlsx"), false, QByteArray("real"));
    QCOMPARE(journal.pendingKeys(), QStringList{key});
    journal.writeNow(key, QStringLiteral("/stub.xlsx"), false, QByteArray());
    QVERIFY(journal.pendingKeys().isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    RecoveryJournalTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_recovery_journal.moc"
