// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaBase app tests: a BaseHost over a real
// database in a temp dir (XDG dirs pointed into it, so backups and
// scratch files never touch the user's), and a realistic seed — the Donors
// & Donations starter with 40 donors, 4 campaigns and 120 donations.

#include "qindabase/src/base_document.h"
#include "qindabase/src/base_host.h"
#include "qindabase/src/base_service.h"

#include "base_file.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

namespace BaseAppTest {

using QindaOffice::Apps::Base::BaseDocument;
using QindaOffice::Apps::Base::BaseHost;
using QindaOffice::Apps::Base::BaseService;
using QindaOffice::Apps::Base::DatasheetModel;

inline QString str(const char *text)
{
    return QString::fromUtf8(text);
}

inline bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

// Deterministic "random" numbers for the seed.
struct Lcg {
    quint32 state = 20260325;
    quint32 next(quint32 bound)
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) % bound;
    }
};

// 40 donors, 4 campaigns, 120 donations (amounts in cents, dates in 2025–2026).
inline bool seedDonors(QindaOffice::Base::BaseFile &file)
{
    static const char *const first[] = {"Ada", "Grace", "Alan", "Katherine", "Dorothy", "Mary", "Frances", "Hedy",
                                        "Rosalind", "Barbara", "Margaret", "Lise", "Chien-Shiung", "Emmy", "Annie",
                                        "Evelyn", "Marie", "Jean", "Sophie", "Radia"};
    static const char *const last[] = {"Lovelace", "Hopper", "Turing", "Johnson", "Vaughan", "Jackson", "Allen",
                                       "Lamarr", "Franklin", "Liskov", "Hamilton", "Meitner", "Wu", "Noether",
                                       "Easley", "Boyd", "Curie", "Bartik", "Wilson", "Perlman"};
    static const char *const cities[] = {"Portland", "Beaverton", "Gresham", "Salem", "Hillsboro", "Eugene"};
    static const char *const types[] = {"Individual", "Individual", "Individual", "Business", "Foundation"};
    static const char *const methods[] = {"Check", "Card", "Cash", "Bank transfer"};
    QindaOffice::Base::Connection &db = file.db();
    Lcg rng;
    bool good = db.exec(str("BEGIN")).ok;
    good = good && db.run(str("INSERT INTO campaigns(name, start_date, goal) VALUES('Spring Appeal 2026', '2026-03-01', 2500000),"
                              "('Year-End Giving 2025', '2025-11-15', 4000000), ('Youth Programs Fund', '2025-06-01', 1500000),"
                              "('Building Repairs', '2026-01-10', 3000000)"))
                      .ok;
    for (int i = 0; i < 40 && good; ++i) {
        const QString name = QStringLiteral("%1 %2").arg(str(first[i % 20]), str(last[(i * 7 + i / 20) % 20]));
        const QString email = name.toLower().replace(QLatin1Char(' '), QLatin1Char('.')) + QStringLiteral("@example.org");
        good = db.run(str("INSERT INTO donors(name, email, phone, city, donor_type, first_gift) VALUES(?, ?, ?, ?, ?, ?)"),
                      {name, email, QStringLiteral("503-555-%1").arg(1000 + i * 37),
                       str(cities[rng.next(6)]), str(types[rng.next(5)]),
                       QStringLiteral("2025-%1-%2").arg(1 + rng.next(12), 2, 10, QLatin1Char('0')).arg(1 + rng.next(28), 2, 10, QLatin1Char('0'))})
                   .ok;
    }
    for (int i = 0; i < 120 && good; ++i) {
        const qint64 amount = (qint64(rng.next(40)) + 1) * 2500 + (rng.next(4) == 0 ? 99 : 0);
        const QString given = QStringLiteral("%1-%2-%3")
                                  .arg(rng.next(3) == 0 ? 2025 : 2026)
                                  .arg(1 + rng.next(rng.next(3) == 0 ? 12 : 3), 2, 10, QLatin1Char('0'))
                                  .arg(1 + rng.next(28), 2, 10, QLatin1Char('0'));
        good = db.run(str("INSERT INTO donations(donor_id, campaign_id, amount, given, method, receipt_sent) "
                          "VALUES(?, ?, ?, ?, ?, ?)"),
                      {qint64(1 + rng.next(40)), rng.next(5) == 0 ? QVariant() : QVariant(qint64(1 + rng.next(4))), amount,
                       given, str(methods[rng.next(4)]), qint64(rng.next(3) != 0 ? 1 : 0)})
                   .ok;
    }
    return db.exec(good ? str("COMMIT") : str("ROLLBACK")).ok && good;
}

} // namespace BaseAppTest

#define BASE_APP_TEST_MAIN(TestClass)                                                      \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir home;                                                                \
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());      \
        qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());        \
        qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());          \
        qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());        \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaBase"));      \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaBase"));           \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
