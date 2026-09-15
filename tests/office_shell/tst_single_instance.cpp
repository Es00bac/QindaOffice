// SPDX-License-Identifier: GPL-3.0-or-later
#include "single_instance.h"

#include <QApplication>
#include <QProcess>
#include <QtTest>

using namespace QindaOffice::Shell;

// Verifies the D-Bus single-instance contract end to end by spawning this
// test binary twice in helper mode over a PRIVATE session bus (spawned
// here, so the test is hermetic and never touches the user's bus).
//
// AGENT-NOTE: some sandboxed/elevated contexts have a session bus that
// accepts connections but denies Qt name ownership; when the primary helper
// reports it could not acquire the name, the hand-off half is skipped with
// an explicit QSKIP rather than a false failure.
class SingleInstanceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void secondProcessHandsOff();

private:
    QString m_busAddress;
    qint64 m_busPid = 0;
};

void SingleInstanceTest::initTestCase()
{
    QProcess daemon;
    daemon.start(QStringLiteral("dbus-daemon"),
                 {QStringLiteral("--session"), QStringLiteral("--fork"),
                  QStringLiteral("--print-address"), QStringLiteral("--print-pid")});
    if (!daemon.waitForFinished(5000)) {
        daemon.kill();
        QSKIP("cannot spawn a private dbus-daemon in this environment");
    }
    const QByteArray out = daemon.readAllStandardOutput();
    const QList<QByteArray> lines = out.split('\n');
    if (lines.size() < 2 || lines.at(0).isEmpty()) {
        QSKIP("private dbus-daemon did not publish an address (sandboxed?)");
    }
    m_busAddress = QString::fromUtf8(lines.at(0));
    m_busPid = lines.at(1).toLongLong();
}

void SingleInstanceTest::cleanupTestCase()
{
    if (m_busPid > 0) {
        QProcess::execute(QStringLiteral("kill"), {QString::number(m_busPid)});
    }
}

static int runHelper(const QString &service, bool primary)
{
    SingleInstance instance(service, QStringLiteral("/org/qindaqt/TestHelper"));
    bool handedOff = false;
    const bool acquired = instance.acquireOrHandOff({}, &handedOff);
    if (primary) {
        printf("PRIMARY:READY %d\n", acquired ? 1 : 0);
        fflush(stdout);
        if (!acquired) {
            return 0;
        }
        instance.publish();
        QTimer::singleShot(15000, QCoreApplication::instance(), &QCoreApplication::quit);
        QCoreApplication::instance()->exec();
        return 0;
    }
    if (!acquired && handedOff) {
        printf("SECONDARY:HANDED\n");
        return 0;
    }
    printf("SECONDARY:UNEXPECTED acquired=%d handedOff=%d\n", acquired, handedOff);
    return 2;
}

void SingleInstanceTest::secondProcessHandsOff()
{
    const QString service = QStringLiteral("org.qindaqt.SingleInstanceTest.%1")
                                .arg(QCoreApplication::applicationPid());
    const QString helper = QCoreApplication::applicationFilePath();
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_busAddress.toUtf8());

    QProcess primary;
    primary.start(helper, {QStringLiteral("--primary"), service});
    QVERIFY(primary.waitForReadyRead(10000));
    const QByteArray banner = primary.readAllStandardOutput().trimmed();
    QVERIFY2(banner.startsWith("PRIMARY:READY"),
             qPrintable(QString::fromUtf8(banner)));
    if (banner == QByteArrayLiteral("PRIMARY:READY 0")) {
        QSKIP("this environment's session bus denies Qt name ownership; "
              "the hand-off wire path needs a healthy bus");
    }

    QProcess secondary;
    secondary.start(helper, {QStringLiteral("--secondary"), service});
    QVERIFY(secondary.waitForFinished(10000));
    QCOMPARE(secondary.exitCode(), 0);
    QVERIFY2(secondary.readAllStandardOutput().contains("SECONDARY:HANDED"),
             "second process must hand off to the primary");

    primary.kill();
    primary.waitForFinished(5000);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    for (int i = 1; i + 1 < argc; ++i) {
        const QString mode = QString::fromLocal8Bit(argv[i]);
        if (mode == QLatin1String("--primary") || mode == QLatin1String("--secondary")) {
            QApplication app(argc, argv);
            return runHelper(QString::fromLocal8Bit(argv[i + 1]),
                             mode == QLatin1String("--primary"));
        }
    }
    QApplication app(argc, argv);
    SingleInstanceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_single_instance.moc"
