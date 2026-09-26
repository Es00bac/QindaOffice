// SPDX-License-Identifier: GPL-3.0-or-later
// Field types: every type parses what people type, refuses what does not
// fit with a reason, and shows stored values back; currency stays exact.

#include "base_fixture.h"

using namespace BaseTest;

class BaseTypesTest final : public QObject {
    Q_OBJECT
private slots:
    void idsAndDeclaredTypesRoundTrip()
    {
        for (const FieldType type : designableTypes()) {
            QCOMPARE(typeFromId(typeId(type)).value(), type);
            const bool rowid = type == FieldType::AutoNumber;
            QCOMPARE(typeFromDeclared(declaredType(type), rowid), type);
        }
        QCOMPARE(typeFromDeclared(str("VARCHAR(40)")), FieldType::Text);
        QCOMPARE(typeFromDeclared(str("DECIMAL(10,2)")), FieldType::Number);
        QCOMPARE(typeFromDeclared(str("TIMESTAMP")), FieldType::DateTime);
        QCOMPARE(typeFromDeclared(str("BLOB")), FieldType::Other);
        QCOMPARE(typeFromDeclared(str("")), FieldType::Text);
        QCOMPARE(typeFromDeclared(str("MONEY")), FieldType::Currency);
    }

    void currencyIsExact()
    {
        const CurrencyFormat usd;
        QCOMPARE(parseMoney(str("$1,234.56"), 2).value(), qint64(123456));
        QCOMPARE(parseMoney(str("(3.50)"), 2).value(), qint64(-350));
        QCOMPARE(parseMoney(str("-0.1"), 2).value(), qint64(-10));
        QCOMPARE(parseMoney(str("12.500"), 2).value(), qint64(1250)); // trailing zeros are fine
        QString error;
        QVERIFY(!parseMoney(str("12.345"), 2, &error));
        QVERIFY(error.contains(str("2 decimal")));
        QVERIFY(!parseMoney(str("abc"), 2));
        QCOMPARE(formatMoney(123456, 2, str("$")), str("$1,234.56"));
        QCOMPARE(formatMoney(-5, 2, str("$")), str("-$0.05"));
        QCOMPARE(formatMoney(std::numeric_limits<qint64>::min(), 2, QString(), false).left(2), str("-9"));
        // 0.1 + 0.2 in money is 0.30, not 0.30000000000000004.
        const qint64 sum = parseInput(FieldType::Currency, str("0.10"), usd).value.toLongLong()
                           + parseInput(FieldType::Currency, str("0.20"), usd).value.toLongLong();
        QCOMPARE(displayText(FieldType::Currency, QVariant::fromValue(sum), usd), str("$0.30"));
        QCOMPARE(editText(FieldType::Currency, QVariant::fromValue(qint64(125000)), usd), str("1250.00"));
        // A QML number arrives as a double.
        QCOMPARE(parseValue(FieldType::Currency, QVariant(19.99), usd).value.toLongLong(), qint64(1999));
        QVERIFY(!parseValue(FieldType::Currency, QVariant(19.999), usd).ok);
        const CurrencyFormat yen{str("¥"), 0};
        QCOMPARE(parseInput(FieldType::Currency, str("1,500"), yen).value.toLongLong(), qint64(1500));
        QCOMPARE(displayText(FieldType::Currency, QVariant::fromValue(qint64(1500)), yen), str("¥1,500"));
    }

    void everyTypeParsesAndRefuses()
    {
        QVERIFY(!parseInput(FieldType::Integer, str("")).value.isValid()); // blank = NULL
        QCOMPARE(parseInput(FieldType::Integer, str("1,200")).value.toLongLong(), qint64(1200));
        QVERIFY(!parseInput(FieldType::Integer, str("1.5")).ok);
        QCOMPARE(parseInput(FieldType::Number, str("2.75")).value.toDouble(), 2.75);
        QVERIFY(!parseInput(FieldType::Number, str("two")).ok);
        QCOMPARE(parseInput(FieldType::YesNo, str("Yes")).value.toLongLong(), qint64(1));
        QCOMPARE(parseInput(FieldType::YesNo, str("false")).value.toLongLong(), qint64(0));
        QVERIFY(!parseInput(FieldType::YesNo, str("maybe")).ok);
        QCOMPARE(parseInput(FieldType::Date, str("3/14/2026")).value.toString(), str("2026-03-14"));
        QCOMPARE(parseInput(FieldType::Date, str("2026-03-14")).value.toString(), str("2026-03-14"));
        QCOMPARE(parseInput(FieldType::Date, str("Mar 14, 2026")).value.toString(), str("2026-03-14"));
        QCOMPARE(parseInput(FieldType::Date, str("25/12/2026")).value.toString(), str("2026-12-25"));
        QVERIFY(!parseInput(FieldType::Date, str("2026-02-30")).ok);
        QCOMPARE(parseInput(FieldType::DateTime, str("2026-03-14 2:05 PM")).value.toString(),
                 str("2026-03-14 14:05:00"));
        QCOMPARE(parseInput(FieldType::DateTime, str("2026-03-14T08:30:15")).value.toString(),
                 str("2026-03-14 08:30:15"));
        QVERIFY(!parseInput(FieldType::DateTime, str("2026-03-14 25:00")).ok);
        QVERIFY(!parseInput(FieldType::Text, QString(300, QLatin1Char('x'))).ok);
        QVERIFY(parseInput(FieldType::LongText, QString(300, QLatin1Char('x'))).ok);
        QCOMPARE(displayText(FieldType::YesNo, QVariant::fromValue(qint64(1))), str("Yes"));
        QCOMPARE(displayText(FieldType::Number, QVariant(3.0)), str("3"));
        QCOMPARE(parseValue(FieldType::YesNo, QVariant(true)).value.toLongLong(), qint64(1));
        QCOMPARE(parseValue(FieldType::Integer, QVariant(42.0)).value.toLongLong(), qint64(42));
        QVERIFY(!parseValue(FieldType::Integer, QVariant(4.5)).ok);
    }
};

BASE_TEST_MAIN(BaseTypesTest)

#include "tst_base_types.moc"
