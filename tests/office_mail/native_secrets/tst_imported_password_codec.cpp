// SPDX-License-Identifier: GPL-3.0-or-later
#include "imported_password_codec.h"

#include <QDataStream>
#include <QIODevice>
#include <QTest>

using namespace QindaOffice::Mail;
namespace {
QByteArray serialize(const QString &text)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << text; // Actual KWallet Entry codec, including current defaults.
    return bytes;
}
}

class ImportedPasswordCodecTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("ascii") << QStringLiteral("synthetic password");
        QTest::newRow("unicode") << QString::fromUtf8("密碼 café 🔒");
        QTest::newRow("whitespace") << QStringLiteral(" leading\ntrailing ");
    }
    void roundTrip()
    {
        QFETCH(QString, text);
        QString decoded;
        QVERIFY(ImportedPassword::decode(serialize(text), decoded));
        QCOMPARE(decoded, text);
    }
    void rejectsInvalid_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("absent") << QByteArray();
        QTest::newRow("null") << serialize(QString());
        QTest::newRow("empty") << serialize(QStringLiteral(""));
        QTest::newRow("truncated-prefix") << QByteArray::fromHex("000002");
        QTest::newRow("truncated-text") << serialize(QStringLiteral("synthetic")).chopped(1);
        QTest::newRow("trailing") << (serialize(QStringLiteral("synthetic")) + QByteArray(1, 'x'));
        QTest::newRow("hostile-declared-size") << QByteArray::fromHex("7ffffffe0061");
        QTest::newRow("extended-size") << QByteArray::fromHex("fffffffe0000000000000002");
        QTest::newRow("odd-size") << QByteArray::fromHex("0000000100");
        QTest::newRow("bad-high-surrogate") << QByteArray::fromHex("00000002d800");
        QTest::newRow("bad-low-surrogate") << QByteArray::fromHex("00000002dc00");
        QTest::newRow("nul") << serialize(QString(QChar(0)));
        QTest::newRow("wrong-endian") << QByteArray::fromHex("020000006100");
        QTest::newRow("oversized") << QByteArray(1024 * 1024 + 1, 'x');
    }
    void rejectsInvalid()
    {
        QFETCH(QByteArray, bytes);
        QString decoded = QStringLiteral("must be cleared");
        QVERIFY(!ImportedPassword::decode(bytes, decoded));
        QVERIFY(decoded.isEmpty());
    }
    void keyScope_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<bool>("valid");
        for (const QString &service : {QStringLiteral("imap"), QStringLiteral("smtp"), QStringLiteral("pop3"),
             QStringLiteral("caldav"), QStringLiteral("ews"), QStringLiteral("ics"),
             QStringLiteral("oauth-refresh-google"), QStringLiteral("oauth-refresh-microsoft")})
            QTest::newRow(qPrintable(service)) << (QStringLiteral("synthetic._-1/") + service) << true;
        QTest::newRow("no-account") << QStringLiteral("/imap") << false;
        QTest::newRow("account-too-long") << (QString(129, QLatin1Char('a')) + QStringLiteral("/imap")) << false;
        QTest::newRow("address") << QStringLiteral("synthetic@example.invalid/imap") << false;
        QTest::newRow("extra-segment") << QStringLiteral("synthetic/extra/imap") << false;
        QTest::newRow("unknown-service") << QStringLiteral("synthetic/password") << false;
        QTest::newRow("upper-service") << QStringLiteral("synthetic/IMAP") << false;
    }
    void keyScope()
    {
        QFETCH(QString, key);
        QFETCH(bool, valid);
        QCOMPARE(ImportedPassword::validKey(key), valid);
    }
};
QTEST_GUILESS_MAIN(ImportedPasswordCodecTest)
#include "tst_imported_password_codec.moc"
