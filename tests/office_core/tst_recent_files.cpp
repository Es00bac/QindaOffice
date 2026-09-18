// SPDX-License-Identifier: GPL-3.0-or-later
#include "recent_files.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

class RecentFilesTest : public QObject {
    Q_OBJECT
private slots:
    void addMoveToFrontAndDedupe();
    void boundIsTwenty();
    void persistsAcrossInstances();
    void removeAndClear();
};

void RecentFilesTest::addMoveToFrontAndDedupe()
{
    QTemporaryDir dir;
    RecentFiles recent;
    recent.load(dir.path() + QStringLiteral("/recent.json"));
    recent.add(QStringLiteral("/a"));
    recent.add(QStringLiteral("/b"));
    recent.add(QStringLiteral("/a")); // dedupe: /a moves to front
    QCOMPARE(recent.paths(), QStringList({QStringLiteral("/a"), QStringLiteral("/b")}));
}

void RecentFilesTest::boundIsTwenty()
{
    QTemporaryDir dir;
    RecentFiles recent;
    recent.load(dir.path() + QStringLiteral("/recent.json"));
    for (int i = 0; i < 25; ++i) {
        recent.add(QStringLiteral("/f%1").arg(i));
    }
    QCOMPARE(recent.paths().size(), RecentFiles::kMaxEntries);
    QCOMPARE(recent.paths().first(), QStringLiteral("/f24"));
    QVERIFY(!recent.paths().contains(QStringLiteral("/f0")));
}

void RecentFilesTest::persistsAcrossInstances()
{
    QTemporaryDir dir;
    const QString store = dir.path() + QStringLiteral("/recent.json");
    {
        RecentFiles recent;
        recent.load(store);
        recent.add(QStringLiteral("/persisted"));
    }
    RecentFiles reopened;
    QVERIFY2(reopened.load(store), "recent.json must round-trip");
    QCOMPARE(reopened.paths(), QStringList({QStringLiteral("/persisted")}));
}

void RecentFilesTest::removeAndClear()
{
    QTemporaryDir dir;
    RecentFiles recent;
    recent.load(dir.path() + QStringLiteral("/recent.json"));
    recent.add(QStringLiteral("/a"));
    recent.add(QStringLiteral("/b"));
    recent.remove(QStringLiteral("/a"));
    QCOMPARE(recent.paths(), QStringList({QStringLiteral("/b")}));
    recent.clear();
    QVERIFY(recent.paths().isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    RecentFilesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_recent_files.moc"
