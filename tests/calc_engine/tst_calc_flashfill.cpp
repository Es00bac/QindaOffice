// SPDX-License-Identifier: GPL-3.0-or-later
// Data ▸ Flash Fill (M78, D-087; flash_fill.h): Excel's everyday patterns
// learned from one or two examples — split, join, case, extract digits,
// initials, the parts of an e-mail address, the first letters — and the
// honest answers when it cannot: an ambiguous pattern, arithmetic, no
// example, nothing to fill.

#include "flash_fill.h"

#include <QElapsedTimer>
#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

QList<QStringList> column(const QStringList &texts)
{
    QList<QStringList> rows;
    for (const QString &text : texts) {
        rows.append(QStringList{text});
    }
    return rows;
}

} // namespace

class CalcFlashFillTest : public QObject {
    Q_OBJECT
private slots:
    void splitsFirstAndLastNames();
    void lastWordFollowsLongerNames();
    void joinsColumnsWithSeparators();
    void changesCase();
    void extractsDigits();
    void makesInitials();
    void splitsEmailAddresses();
    void takesTheFirstLetters();
    void describesWhatItInferred();
    void anotherExampleSettlesAnAmbiguity();
    void saysWhenTheExamplesAreAmbiguous();
    void saysWhatItCannotInfer();
    void needsExamplesAndRowsToFill();
    void staysQuickOnLongColumnsAndLongText();
};

void CalcFlashFillTest::splitsFirstAndLastNames()
{
    const QList<QStringList> names =
        column({QStringLiteral("John Smith"), QStringLiteral("Mary Johnson"), QStringLiteral("Ann Lee"), QString(),
                QStringLiteral("Bob Stone")});
    const FlashFillResult first = flashFill(names, {{0, QStringLiteral("John")}});
    QVERIFY2(first.ok, qPrintable(first.failure));
    QCOMPARE(first.outputs, (QStringList{QStringLiteral("John"), QStringLiteral("Mary"), QStringLiteral("Ann"), QString(),
                                         QStringLiteral("Bob")}));
    QCOMPARE(first.filled, 3); // the example and the empty row are not filled
    const FlashFillResult last = flashFill(names, {{0, QStringLiteral("Smith")}});
    QVERIFY2(last.ok, qPrintable(last.failure));
    QCOMPARE(last.outputs.at(1), QStringLiteral("Johnson"));
    QCOMPARE(last.outputs.at(4), QStringLiteral("Stone"));
}

void CalcFlashFillTest::lastWordFollowsLongerNames()
{
    // The last name is the last word, also of a name with a middle name.
    const FlashFillResult result =
        flashFill(column({QStringLiteral("John Smith"), QStringLiteral("Mary Ann Lee")}), {{0, QStringLiteral("Smith")}});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.outputs.at(1), QStringLiteral("Lee"));
}

void CalcFlashFillTest::joinsColumnsWithSeparators()
{
    const QList<QStringList> people{{QStringLiteral("John"), QStringLiteral("Smith")},
                                    {QStringLiteral("Mary"), QStringLiteral("Johnson")},
                                    {QStringLiteral("Li"), QStringLiteral("Wei")}};
    const FlashFillResult comma = flashFill(people, {{0, QStringLiteral("Smith, John")}});
    QVERIFY2(comma.ok, qPrintable(comma.failure));
    QCOMPARE(comma.outputs.at(1), QStringLiteral("Johnson, Mary"));
    QCOMPARE(comma.outputs.at(2), QStringLiteral("Wei, Li"));
    const FlashFillResult email = flashFill(people, {{0, QStringLiteral("john.smith@example.com")}});
    QVERIFY2(email.ok, qPrintable(email.failure));
    QCOMPARE(email.outputs.at(1), QStringLiteral("mary.johnson@example.com"));
    QCOMPARE(email.outputs.at(2), QStringLiteral("li.wei@example.com"));
}

void CalcFlashFillTest::changesCase()
{
    const FlashFillResult upper = flashFill(column({QStringLiteral("john smith"), QStringLiteral("mary jones")}),
                                            {{0, QStringLiteral("JOHN SMITH")}});
    QVERIFY2(upper.ok, qPrintable(upper.failure));
    QCOMPARE(upper.outputs.at(1), QStringLiteral("MARY JONES"));
    const FlashFillResult proper = flashFill(column({QStringLiteral("jOHN sMITH"), QStringLiteral("o'brien-SMITH")}),
                                             {{0, QStringLiteral("John Smith")}});
    QVERIFY2(proper.ok, qPrintable(proper.failure));
    QCOMPARE(proper.outputs.at(1), QStringLiteral("O'Brien-Smith")); // Excel's PROPER
    const FlashFillResult lower = flashFill(column({QStringLiteral("John SMITH"), QStringLiteral("Mary JONES")}),
                                            {{0, QStringLiteral("john smith")}});
    QVERIFY2(lower.ok, qPrintable(lower.failure));
    QCOMPARE(lower.outputs.at(1), QStringLiteral("mary jones"));
}

void CalcFlashFillTest::extractsDigits()
{
    const FlashFillResult result = flashFill(
        column({QStringLiteral("Order #4521"), QStringLiteral("Order #77"), QStringLiteral("Order #123456 rush")}),
        {{0, QStringLiteral("4521")}});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.outputs.at(1), QStringLiteral("77"));
    QCOMPARE(result.outputs.at(2), QStringLiteral("123456"));
}

void CalcFlashFillTest::makesInitials()
{
    const FlashFillResult result = flashFill(column({QStringLiteral("John Smith"), QStringLiteral("Mary Jones")}),
                                             {{0, QStringLiteral("J. Smith")}});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.outputs.at(1), QStringLiteral("M. Jones"));
    const FlashFillResult letters = flashFill(
        column({QStringLiteral("john smith"), QStringLiteral("ada lovelace")}), {{0, QStringLiteral("JS")}});
    QVERIFY2(letters.ok, qPrintable(letters.failure));
    QCOMPARE(letters.outputs.at(1), QStringLiteral("AL"));
}

void CalcFlashFillTest::splitsEmailAddresses()
{
    const QList<QStringList> addresses = column(
        {QStringLiteral("smith.john@example.com"), QStringLiteral("ann.lee@mail.org"), QStringLiteral("bob@acme.co.uk")});
    const FlashFillResult domain = flashFill(addresses, {{0, QStringLiteral("example.com")}});
    QVERIFY2(domain.ok, qPrintable(domain.failure));
    QCOMPARE(domain.outputs.at(1), QStringLiteral("mail.org"));
    QCOMPARE(domain.outputs.at(2), QStringLiteral("acme.co.uk"));
    const FlashFillResult user = flashFill(addresses, {{0, QStringLiteral("smith.john")}});
    QVERIFY2(user.ok, qPrintable(user.failure));
    QCOMPARE(user.outputs.at(1), QStringLiteral("ann.lee"));
    QCOMPARE(user.outputs.at(2), QStringLiteral("bob"));
}

void CalcFlashFillTest::takesTheFirstLetters()
{
    const FlashFillResult result = flashFill(
        column({QStringLiteral("Johnson"), QStringLiteral("Williams"), QStringLiteral("Brown")}), {{0, QStringLiteral("Joh")}});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.outputs, (QStringList{QStringLiteral("Joh"), QStringLiteral("Wil"), QStringLiteral("Bro")}));
}

void CalcFlashFillTest::describesWhatItInferred()
{
    const QList<QStringList> people{{QStringLiteral("John"), QStringLiteral("Smith")},
                                    {QStringLiteral("Mary"), QStringLiteral("Johnson")}};
    const FlashFillResult result =
        flashFill(people, {{0, QStringLiteral("Smith, John")}}, {QStringLiteral("First"), QStringLiteral("Last")});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.pattern, QStringLiteral("Last, then “, ”, then First"));
    const FlashFillResult last = flashFill(column({QStringLiteral("John Smith"), QStringLiteral("Mary Jones")}),
                                           {{0, QStringLiteral("SMITH")}});
    QVERIFY2(last.ok, qPrintable(last.failure));
    QCOMPARE(last.pattern, QStringLiteral("the last word of column 1, in capitals"));
}

void CalcFlashFillTest::anotherExampleSettlesAnAmbiguity()
{
    // Both columns read "x1" in the first row; the second example says B.
    const QList<QStringList> rows{{QStringLiteral("x1"), QStringLiteral("x1")},
                                  {QStringLiteral("y"), QStringLiteral("z")},
                                  {QStringLiteral("w"), QStringLiteral("v")}};
    const FlashFillResult result = flashFill(rows, {{0, QStringLiteral("x1")}, {1, QStringLiteral("z")}});
    QVERIFY2(result.ok, qPrintable(result.failure));
    QCOMPARE(result.outputs.at(2), QStringLiteral("v"));
    QCOMPARE(result.filled, 1);
}

void CalcFlashFillTest::saysWhenTheExamplesAreAmbiguous()
{
    const QList<QStringList> rows{{QStringLiteral("x1"), QStringLiteral("x1")}, {QStringLiteral("y"), QStringLiteral("z")}};
    const FlashFillResult result = flashFill(rows, {{0, QStringLiteral("x1")}});
    QVERIFY(!result.ok);
    QCOMPARE(result.ambiguousRow, 1);
    QVERIFY2(result.failure.contains(QStringLiteral("more than one pattern")), qPrintable(result.failure));
    QVERIFY(result.outputs.at(1).isEmpty()); // nothing filled
    QCOMPARE(result.filled, 0);
}

void CalcFlashFillTest::saysWhatItCannotInfer()
{
    // 10 + 5 = 15 and 3 + 4 = 7: arithmetic, which Flash Fill cannot do.
    const QList<QStringList> rows{{QStringLiteral("10"), QStringLiteral("5")},
                                  {QStringLiteral("3"), QStringLiteral("4")},
                                  {QStringLiteral("7"), QStringLiteral("8")}};
    const FlashFillResult result = flashFill(rows, {{0, QStringLiteral("15")}, {1, QStringLiteral("7")}});
    QVERIFY(!result.ok);
    QVERIFY2(result.failure.contains(QStringLiteral("arithmetic")), qPrintable(result.failure));
    QVERIFY(result.outputs.at(2).isEmpty());
    // A constant is not a pattern either.
    const FlashFillResult constant =
        flashFill(column({QStringLiteral("abc"), QStringLiteral("def")}), {{0, QStringLiteral("Yes")}});
    QVERIFY(!constant.ok);
}

void CalcFlashFillTest::needsExamplesAndRowsToFill()
{
    const FlashFillResult none = flashFill(column({QStringLiteral("a b"), QStringLiteral("c d")}), {});
    QVERIFY(!none.ok);
    QVERIFY(!none.failure.isEmpty());
    const FlashFillResult full = flashFill(column({QStringLiteral("a b")}), {{0, QStringLiteral("a")}});
    QVERIFY(!full.ok);
    QVERIFY(!full.failure.isEmpty());
}

void CalcFlashFillTest::staysQuickOnLongColumnsAndLongText()
{
    QList<QStringList> rows;
    for (int i = 0; i < 5000; ++i) {
        rows.append({QStringLiteral("Person%1 Family%2").arg(i).arg(i % 97)});
    }
    QElapsedTimer timer;
    timer.start();
    const FlashFillResult names = flashFill(rows, {{0, QStringLiteral("Family0, Person0")}});
    QVERIFY2(names.ok, qPrintable(names.failure));
    QCOMPARE(names.outputs.at(4999), QStringLiteral("Family52, Person4999"));
    // Long sentences with two examples: answered (either way) in bounded time.
    const QString filler = QStringLiteral("the quick brown fox jumps over the lazy dog while the band plays on ");
    const QList<QStringList> text{{filler + QStringLiteral("alpha 17")}, {filler + QStringLiteral("beta 4")},
                                  {filler + QStringLiteral("gamma 99")}};
    const FlashFillResult tail = flashFill(text, {{0, filler.toUpper() + QStringLiteral("17")},
                                                  {1, filler.toUpper() + QStringLiteral("4")}});
    QVERIFY2(tail.ok, qPrintable(tail.failure));
    QCOMPARE(tail.outputs.at(2), filler.toUpper() + QStringLiteral("99"));
    QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
}

QTEST_MAIN(CalcFlashFillTest)
#include "tst_calc_flashfill.moc"
