// SPDX-License-Identifier: GPL-3.0-or-later
#include "atomic_file_store.h"

#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

class AtomicFileStoreTest : public QObject {
    Q_OBJECT
private slots:
    void writeThenReadRoundTrips();
    void revisionDetectsExternalChange();
    void missingFileHasZeroRevision();
};

void AtomicFileStoreTest::writeThenReadRoundTrips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/doc.txt");
    const AtomicFileStore::Result result =
        AtomicFileStore::writeText(path, QStringLiteral("héllo\n"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("héllo\n"));
}

void AtomicFileStoreTest::revisionDetectsExternalChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/doc.txt");
    QVERIFY(AtomicFileStore::writeText(path, QStringLiteral("one")).ok);
    const quint64 first = fileRevision(path);
    QVERIFY(first != 0);
    QCOMPARE(fileRevision(path), first); // stable when untouched
    QVERIFY(AtomicFileStore::writeText(path, QStringLiteral("two and longer")).ok);
    QVERIFY(fileRevision(path) != first);
}

void AtomicFileStoreTest::missingFileHasZeroRevision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(fileRevision(dir.path() + QStringLiteral("/nope.txt")), quint64(0));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    AtomicFileStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_atomic_file_store.moc"
