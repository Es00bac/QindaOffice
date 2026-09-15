// SPDX-License-Identifier: GPL-3.0-or-later
#include "atomic_file_store.h"
#include "plain_text_document.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

class PlainTextDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void saveLoadRoundTrip();
    void dirtyTransitionsSignal();
    void snapshotRestoreKeepsDirtyState();
    void byteRevisionRefreshesOnSave();
};

void PlainTextDocumentTest::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/doc.txt");

    PlainTextDocument source;
    source.setText(QStringLiteral("payload"));
    QVERIFY(source.save(path));

    PlainTextDocument target;
    QString error;
    QVERIFY2(target.load(path, &error), qPrintable(error));
    QCOMPARE(target.text(), QStringLiteral("payload"));
    QCOMPARE(target.canonicalPath(), path);
    QCOMPARE(target.displayTitle(), QStringLiteral("doc.txt"));
    QVERIFY(!target.isDirty());
}

void PlainTextDocumentTest::dirtyTransitionsSignal()
{
    PlainTextDocument doc;
    QSignalSpy spy(&doc, &Document::dirtyChanged);
    QVERIFY(!doc.isDirty());
    doc.setText(QStringLiteral("x"));
    QVERIFY(doc.isDirty());
    QCOMPARE(spy.count(), 1);
    doc.setText(QStringLiteral("x")); // unchanged: no re-signal
    QCOMPARE(spy.count(), 1);
}

void PlainTextDocumentTest::snapshotRestoreKeepsDirtyState()
{
    PlainTextDocument doc;
    doc.setText(QStringLiteral("unsaved work"));
    const QByteArray snapshot = doc.recoverySnapshot();
    QCOMPARE(snapshot, QByteArray("unsaved work"));

    PlainTextDocument restored;
    QString error;
    QVERIFY2(restored.loadRecovery(snapshot, &error), qPrintable(error));
    QCOMPARE(restored.text(), QStringLiteral("unsaved work"));
    QVERIFY(restored.isDirty()); // a restored snapshot is unsaved by definition
}

void PlainTextDocumentTest::byteRevisionRefreshesOnSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/doc.txt");
    PlainTextDocument doc;
    QCOMPARE(doc.byteRevision(), quint64(0));
    doc.setText(QStringLiteral("v1"));
    QVERIFY(doc.save(path));
    const quint64 afterSave = doc.byteRevision();
    QVERIFY(afterSave != 0);
    QVERIFY(AtomicFileStore::writeText(path, QStringLiteral("external")).ok);
    QVERIFY(fileRevision(path) != afterSave); // save-time check would fire
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    PlainTextDocumentTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plain_text_document.moc"
