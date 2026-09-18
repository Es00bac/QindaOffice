// SPDX-License-Identifier: GPL-3.0-or-later
#include "restore_inventory.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

class RestoreInventoryTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsToDisabled();
    void persistLoadRoundTrip();
    void pathBoundAndActiveIndexClamp();
    void exceeds64kFails();
};

void RestoreInventoryTest::defaultsToDisabled()
{
    QTemporaryDir dir;
    RestoreInventory inventory(dir.path() + QStringLiteral("/inv.json"));
    QVERIFY(!inventory.enabled());
    QVERIFY(inventory.snapshot().paths.isEmpty());
}

void RestoreInventoryTest::persistLoadRoundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/inv.json");
    {
        RestoreInventory inventory(path);
        inventory.setEnabled(true);
        RestoreInventory::Snapshot snapshot;
        snapshot.paths = {QStringLiteral("/x"), QStringLiteral("/y")};
        snapshot.activeIndex = 1;
        inventory.setSnapshot(snapshot);
        QVERIFY(inventory.persist());
    }
    RestoreInventory reopened(path);
    QVERIFY(reopened.enabled());
    QCOMPARE(reopened.snapshot().paths.size(), 2);
    QCOMPARE(reopened.snapshot().paths.at(1), QStringLiteral("/y"));
    QCOMPARE(reopened.snapshot().activeIndex, 1);
}

void RestoreInventoryTest::pathBoundAndActiveIndexClamp()
{
    QTemporaryDir dir;
    RestoreInventory inventory(dir.path() + QStringLiteral("/inv.json"));
    RestoreInventory::Snapshot snapshot;
    for (int i = 0; i < 40; ++i) {
        snapshot.paths.append(QStringLiteral("/p%1").arg(i));
    }
    snapshot.activeIndex = 99;
    inventory.setSnapshot(snapshot);
    QCOMPARE(inventory.snapshot().paths.size(), RestoreInventory::kMaxPaths);
    QCOMPARE(inventory.snapshot().activeIndex, RestoreInventory::kMaxPaths - 1);
}

void RestoreInventoryTest::exceeds64kFails()
{
    QTemporaryDir dir;
    RestoreInventory inventory(dir.path() + QStringLiteral("/inv.json"));
    RestoreInventory::Snapshot snapshot;
    // 32 paths x ~3 KiB each blow the 64 KiB file bound.
    const QString fat = QStringLiteral("/%1").arg(QString(3000, QLatin1Char('a')));
    for (int i = 0; i < RestoreInventory::kMaxPaths; ++i) {
        snapshot.paths.append(fat + QString::number(i));
    }
    inventory.setSnapshot(snapshot);
    QString error;
    QVERIFY(!inventory.persist(&error));
    QVERIFY(!error.isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    RestoreInventoryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_restore_inventory.moc"
