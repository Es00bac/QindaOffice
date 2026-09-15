// SPDX-License-Identifier: GPL-3.0-or-later
// DesktopSettings gates: with no settings service on the session bus (the
// offscreen test environment) reads degrade to empty/false instead of
// erroring; the helper never hard-depends on the desktop.

#include "desktop_settings.h"

#include <QCoreApplication>
#include <QtTest>

using namespace QindaOffice::Shell;

class DesktopSettingsTest : public QObject {
    Q_OBJECT
private slots:
    void missingServiceDegradesClosed();
};

void DesktopSettingsTest::missingServiceDegradesClosed()
{
    // The offscreen test session has no org.qindaqt.Settings1 owner; both
    // entry points must fail closed and cheap.
    QVERIFY(DesktopSettings::values({QStringLiteral("accessibility.reducedMotion")}).isEmpty());
    QVERIFY(!DesktopSettings::reducedMotion());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    DesktopSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_desktop_settings.moc"
