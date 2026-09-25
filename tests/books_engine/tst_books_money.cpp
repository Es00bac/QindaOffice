// SPDX-License-Identifier: GPL-3.0-or-later
// Money and Decimal: exact arithmetic, explicit rounding, allocation that
// never loses a cent, locale parse/format — and a source scan proving no
// floating-point type is used anywhere in books_engine (D-045).
#include "decimal.h"
#include "money.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

#include <limits>

using namespace QindaOffice::Books;

class TstBooksMoney : public QObject {
    Q_OBJECT
private slots:
    void pointOnePlusPointTwo()
    {
        const Money a = *Money::parse(QStringLiteral("0.1"), QStringLiteral("USD"));
        const Money b = *Money::parse(QStringLiteral("0.2"), QStringLiteral("USD"));
        QCOMPARE((a + b).minor(), 30);
        QCOMPARE((a + b).toPlainString(), QStringLiteral("0.30"));
        QCOMPARE(a + b, *Money::parse(QStringLiteral("0.3"), QStringLiteral("USD")));
    }

    void largeAndNegativeValues()
    {
        const Money big(5'000'000'000'000'000'000LL, QStringLiteral("USD")); // 5e16 dollars
        QCOMPARE(big.toPlainString(), QStringLiteral("50000000000000000.00"));
        QVERIFY(!big.checkedAdd(big).has_value()); // overflow is detected, never wraps
        const Money min(std::numeric_limits<qint64>::min(), QStringLiteral("USD"));
        QCOMPARE(min.toPlainString(), QStringLiteral("-92233720368547758.08"));
        const Money n(-1234567, QStringLiteral("USD"));
        QCOMPARE(n.toPlainString(), QStringLiteral("-12345.67"));
        QCOMPARE(n.abs().minor(), 1234567);
        QCOMPARE((-n).minor(), 1234567);
        QCOMPARE(Money(-5, QStringLiteral("USD")).toPlainString(), QStringLiteral("-0.05"));
    }

    void currencyExponents()
    {
        QCOMPARE(currencyInfo(QStringLiteral("JPY")).exponent, 0);
        QCOMPARE(currencyInfo(QStringLiteral("KWD")).exponent, 3);
        QCOMPARE(currencyInfo(QStringLiteral("eur")).code, QStringLiteral("EUR"));
        QCOMPARE(Money(1500, QStringLiteral("JPY")).toPlainString(), QStringLiteral("1500"));
        QCOMPARE(Money(1500, QStringLiteral("KWD")).toPlainString(), QStringLiteral("1.500"));
        QCOMPARE(currencyInfo(QStringLiteral("XYZ")).exponent, 2); // unknown: 2, still usable
        QVERIFY(!Money::parse(QStringLiteral("1.5"), QStringLiteral("JPY")).has_value());
        QCOMPARE(Money::parse(QStringLiteral("1.234"), QStringLiteral("KWD"))->minor(), 1234);
    }

    void decimalParse()
    {
        QCOMPARE(Decimal::parse(QStringLiteral("8.25%"))->toString(), QStringLiteral("0.0825"));
        QCOMPARE(Decimal::parse(QStringLiteral("1.50"))->toString(), QStringLiteral("1.5"));
        QCOMPARE(Decimal::parse(QStringLiteral("-0.125"))->mantissa(), -125);
        QVERIFY(!Decimal::parse(QStringLiteral("1e3")).has_value());
        QVERIFY(!Decimal::parse(QStringLiteral("1,000")).has_value());
        QVERIFY(!Decimal::parse(QStringLiteral("")).has_value());
        QVERIFY(!Decimal::parse(QStringLiteral("1.2.3")).has_value());
        QCOMPARE(Decimal::parse(QStringLiteral("0.0825"))->toPercentString(), QStringLiteral("8.25%"));
        QVERIFY(*Decimal::parse(QStringLiteral("1.50")) == *Decimal::parse(QStringLiteral("1.5")));
    }

    void multiplyWithRounding_data()
    {
        QTest::addColumn<qint64>("minor");
        QTest::addColumn<QString>("factor");
        QTest::addColumn<int>("mode");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("tax 8.25% of 19.99 half-up") << qint64(1999) << QStringLiteral("8.25%") << 0 << qint64(165);
        QTest::newRow("1.5 x 3.33") << qint64(333) << QStringLiteral("1.5") << 0 << qint64(500); // 4.995 -> 5.00
        QTest::newRow("half-up tie away") << qint64(5) << QStringLiteral("0.5") << 0 << qint64(3);
        QTest::newRow("half-even tie to even") << qint64(5) << QStringLiteral("0.5") << 1 << qint64(2);
        QTest::newRow("half-even odd") << qint64(7) << QStringLiteral("0.5") << 1 << qint64(4);
        QTest::newRow("negative half-up") << qint64(-5) << QStringLiteral("0.5") << 0 << qint64(-3);
        QTest::newRow("toward zero") << qint64(999) << QStringLiteral("0.5") << 2 << qint64(499);
        QTest::newRow("away from zero") << qint64(999) << QStringLiteral("0.5") << 3 << qint64(500);
        QTest::newRow("exact") << qint64(10000) << QStringLiteral("7.5%") << 0 << qint64(750);
    }

    void multiplyWithRounding()
    {
        QFETCH(qint64, minor);
        QFETCH(QString, factor);
        QFETCH(int, mode);
        QFETCH(qint64, expected);
        const Money m(minor, QStringLiteral("USD"));
        QCOMPARE(m.multiplied(*Decimal::parse(factor), RoundingMode(mode)).minor(), expected);
    }

    void allocationKeepsEveryCent()
    {
        const Money total(10000, QStringLiteral("USD"));
        QList<Money> thirds = total.split(3);
        QCOMPARE(thirds.size(), 3);
        QCOMPARE(thirds[0].minor(), 3334);
        QCOMPARE(thirds[1].minor(), 3333);
        QCOMPARE(thirds[2].minor(), 3333);
        const QList<Money> parts = Money(1000, QStringLiteral("USD")).allocate({1, 1, 1});
        QCOMPARE(parts[0].minor() + parts[1].minor() + parts[2].minor(), 1000);
        // Weighted, remainder to the largest fractional share.
        const QList<Money> w = Money(100, QStringLiteral("USD")).allocate({70, 20, 10, 0});
        QCOMPARE(w[0].minor(), 70);
        QCOMPARE(w[3].minor(), 0);
        const QList<Money> odd = Money(-1001, QStringLiteral("USD")).allocate({1, 2});
        QCOMPARE(odd[0].minor() + odd[1].minor(), -1001);
        QCOMPARE(odd[1].minor(), -667);
        // Stress: any total, any weights, the sum is exact and each part is
        // within one minor unit of its exact share.
        for (qint64 amount : {1LL, 7LL, 99LL, 12345LL, 1000001LL}) {
            const QList<qint64> weights{3, 5, 7, 11, 13};
            const QList<Money> p = Money(amount, QStringLiteral("USD")).allocate(weights);
            qint64 sum = 0;
            for (int i = 0; i < p.size(); ++i) {
                sum += p[i].minor();
                const qint64 exactTimes39 = amount * weights[i];
                QVERIFY(qAbs(p[i].minor() * 39 - exactTimes39) < 39);
            }
            QCOMPARE(sum, amount);
        }
        QCOMPARE(Money(10, QStringLiteral("USD")).allocate({0, 0}).at(0).minor(), 5);
    }

    void parseLocales()
    {
        QCOMPARE(Money::parse(QStringLiteral("$1,234.56"), QStringLiteral("USD"), QLocale(QLocale::English, QLocale::UnitedStates))->minor(),
                 123456);
        QCOMPARE(Money::parse(QStringLiteral("(45.00)"), QStringLiteral("USD"))->minor(), -4500);
        QCOMPARE(Money::parse(QStringLiteral("-45"), QStringLiteral("USD"))->minor(), -4500);
        QCOMPARE(Money::parse(QStringLiteral("45-"), QStringLiteral("USD"))->minor(), -4500);
        QCOMPARE(Money::parse(QStringLiteral("1.234,56 €"), QStringLiteral("EUR"), QLocale(QLocale::German, QLocale::Germany))->minor(),
                 123456);
        QCOMPARE(Money::parse(QStringLiteral("USD 12"), QStringLiteral("USD"))->minor(), 1200);
        QVERIFY(!Money::parse(QStringLiteral("12.345"), QStringLiteral("USD")).has_value()); // refused, not rounded
        QCOMPARE(Money::parse(QStringLiteral("12.3400"), QStringLiteral("USD"))->minor(), 1234);
        QVERIFY(!Money::parse(QStringLiteral("abc"), QStringLiteral("USD")).has_value());
        QVERIFY(!Money::parse(QStringLiteral("--5"), QStringLiteral("USD")).has_value());
    }

    void formatLocales()
    {
        const Money m(-123456, QStringLiteral("USD"));
        const QString us = m.format(QLocale(QLocale::English, QLocale::UnitedStates));
        QVERIFY2(us.contains(QStringLiteral("1,234.56")) && us.contains(QLatin1Char('$')), qPrintable(us));
        QVERIFY(us.contains(QLatin1Char('-')) || us.contains(QLatin1Char('(')));
        const QString de = Money(123456, QStringLiteral("EUR")).format(QLocale(QLocale::German, QLocale::Germany));
        QVERIFY2(de.contains(QStringLiteral("1.234,56")) && de.contains(QStringLiteral("€")), qPrintable(de));
        QCOMPARE(Money(5, QStringLiteral("USD")).format(QLocale::c(), false), QStringLiteral("0.05"));
        QCOMPARE(Money(700, QStringLiteral("JPY")).format(QLocale::c(), false), QStringLiteral("700"));
        // A 7 in the amount must not confuse the symbol-template splice.
        QVERIFY(Money(777777, QStringLiteral("USD")).format(QLocale(QLocale::English, QLocale::UnitedStates))
                    .contains(QStringLiteral("7,777.77")));
    }

    void noFloatingPointInTheEngine()
    {
        // AGENT-GUARD proof: no double/float type anywhere in the engine.
        const QRegularExpression forbidden(QStringLiteral("\\b(double|float|qreal)\\b"));
        QDirIterator it(QStringLiteral(SOURCE_DIR "/../../libs/books_engine/src"), {QStringLiteral("*.h"), QStringLiteral("*.cpp")},
                        QDir::Files);
        int scanned = 0;
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            int lineNo = 0;
            for (const QByteArray &raw : f.readAll().split('\n')) {
                ++lineNo;
                QString line = QString::fromUtf8(raw);
                const qsizetype comment = line.indexOf(QStringLiteral("//"));
                if (comment >= 0) {
                    line.truncate(comment);
                }
                QVERIFY2(!forbidden.match(line).hasMatch(),
                         qPrintable(QStringLiteral("%1:%2 uses a floating-point type").arg(f.fileName()).arg(lineNo)));
            }
            ++scanned;
        }
        QVERIFY(scanned > 20);
    }
};

QTEST_GUILESS_MAIN(TstBooksMoney)
#include "tst_books_money.moc"
