// SPDX-License-Identifier: GPL-3.0-or-later
// The tax-year data file (payroll_tables.h): it loads, every schedule is
// internally consistent and agrees with the other publication it derives
// from, a missing year is refused in plain words, a damaged file is
// refused with the path of the bad value, unconfirmed values are listed —
// and no floating-point type exists anywhere in books_payroll.
#include "payroll_calendar.h"
#include "payroll_tables.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

QByteArray bundled2026()
{
    QFile f(QStringLiteral(":/books_payroll/payroll_2026.json"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Each row's base is the previous base plus the previous rate on the
// previous row's width — exactly, to the cent.
QString inconsistency(const RateSchedule &s)
{
    for (qsizetype i = 1; i < s.size(); ++i) {
        const Bracket &p = s.at(i - 1);
        const Money expected = p.base + (s.at(i).over - p.over).multiplied(p.rate, RoundingMode::HalfUp);
        if (expected != s.at(i).base) {
            return QStringLiteral("row %1: %2 printed, %3 computed").arg(i).arg(s.at(i).base.toPlainString(),
                                                                           expected.toPlainString());
        }
    }
    return {};
}

} // namespace

class TstPayrollTables : public QObject {
    Q_OBJECT
    PayrollTables t;

private slots:
    void initTestCase()
    {
        QString error;
        const std::optional<PayrollTables> tables = PayrollTables::forYear(2026, &error);
        QVERIFY2(tables.has_value(), qPrintable(error));
        t = *tables;
    }

    void loadsTheYearWithItsSources()
    {
        QCOMPARE(t.year(), 2026);
        QVERIFY(PayrollTables::availableYears().contains(2026));
        QStringList ids;
        for (const TableSource &s : t.sources()) {
            QVERIFY2(s.url.startsWith(QStringLiteral("https://")), qPrintable(s.id));
            QVERIFY2(!s.title.isEmpty() && !s.revised.isEmpty() && !s.checked.isEmpty(), qPrintable(s.id));
            ids << s.id;
        }
        for (const char *id : {"p15t", "p15", "ssa", "f1040es", "rp2025-32", "dr1098", "dr0004"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
        // Spot values (docs/payroll-tax-tables.md).
        QCOMPARE(t.fica().socialSecurityWageBase, m("184500"));
        QCOMPARE(t.fica().socialSecurityRate, *Decimal::parse(QStringLiteral("6.2%")));
        QCOMPARE(t.fica().additionalMedicareThreshold, m("200000"));
        QCOMPARE(t.futa().wageBase, m("7000"));
        QCOMPARE(t.federalWithholding().payPeriods.value(PayFrequency::Biweekly), 26);
        QVERIFY(!t.federalWithholding().payPeriods.contains(PayFrequency::Annual)); // not in Table 3
        QCOMPARE(t.federalWithholding().allowanceValue, m("4300"));
        QCOMPARE(t.estimatedTax().standardDeduction.value(FilingStatus::MarriedJointly), m("32200"));
        QCOMPARE(t.estimatedTax().dueDates.size(), 4);
        QCOMPARE(t.estimatedTax().dueDates.last(), QDate(2027, 1, 15));
        QCOMPARE(t.colorado().incomeTaxRate, *Decimal::parse(QStringLiteral("4.4%")));
        QCOMPARE(t.colorado().withholding.defaultAllowanceMarriedJointly, m("11000"));
        QCOMPARE(t.colorado().uiWageBase, m("30600"));
        QCOMPARE(t.colorado().famliRate, *Decimal::parse(QStringLiteral("0.88%")));
        QCOMPARE(t.colorado().famliWageBase, t.fica().socialSecurityWageBase);
        QCOMPARE(t.retirement().deferral401k, m("24500"));
        QStringList cities;
        for (const OccupationalPrivilegeTax &c : t.colorado().opt) {
            cities << c.key;
        }
        cities.sort();
        QCOMPARE(cities, QStringList({QStringLiteral("aurora"), QStringLiteral("denver"), QStringLiteral("glendale"),
                                      QStringLiteral("greenwoodVillage"), QStringLiteral("sheridan")}));
    }

    void percentageTablesAreConsistent()
    {
        const FederalWithholdingTables &w = t.federalWithholding();
        for (const W4Status s : {W4Status::MarriedJointly, W4Status::SingleOrMarriedSeparately, W4Status::HeadOfHousehold}) {
            QVERIFY2(inconsistency(w.standard.value(s)).isEmpty(), qPrintable(inconsistency(w.standard.value(s))));
        }
        QVERIFY(inconsistency(w.step2Checkbox.value(W4Status::MarriedJointly)).isEmpty());
        QVERIFY(inconsistency(w.step2Checkbox.value(W4Status::HeadOfHousehold)).isEmpty());
        // AGENT-NOTE: the Step 2 Single table's thresholds are rounded to
        // the dollar ($108,938, $136,163, $328,350), so the IRS prints its
        // bases as exactly half the STANDARD Single bases, not as the
        // recomputation ($20,512.00 printed vs $20,512.12). The file keeps
        // the printed values; this pins that relation.
        const RateSchedule &half = w.step2Checkbox.value(W4Status::SingleOrMarriedSeparately);
        const RateSchedule &single = w.standard.value(W4Status::SingleOrMarriedSeparately);
        QCOMPARE(half.size(), single.size());
        for (qsizetype i = 0; i < half.size(); ++i) {
            QCOMPARE(half.at(i).base, single.at(i).base.scaled(1, 2, RoundingMode::HalfUp));
            QCOMPARE(half.at(i).rate, single.at(i).rate);
        }
        // A 2019-or-earlier form uses the STANDARD rows (Pub 15-T section 5).
        QCOMPARE(w.legacyMarried.size(), w.standard.value(W4Status::MarriedJointly).size());
        for (qsizetype i = 0; i < w.legacySingle.size(); ++i) {
            QCOMPARE(w.legacySingle.at(i).over, single.at(i).over);
            QCOMPARE(w.legacySingle.at(i).base, single.at(i).base);
        }
    }

    // Two documents published separately must agree: the STANDARD
    // withholding thresholds are the 1040-ES brackets shifted by the
    // standard deduction less Worksheet 1A line 1g (MFJ 32,200 − 12,900 =
    // 19,300; Single 16,100 − 8,600 = 7,500; HoH 24,150 − 8,600 = 15,550),
    // with the same bases and rates.
    void withholdingTablesMatchTheRateSchedules()
    {
        const struct {
            W4Status w4;
            FilingStatus filing;
            Money shift;
        } pairs[] = {{W4Status::MarriedJointly, FilingStatus::MarriedJointly, m("19300")},
                     {W4Status::SingleOrMarriedSeparately, FilingStatus::Single, m("7500")},
                     {W4Status::HeadOfHousehold, FilingStatus::HeadOfHousehold, m("15550")}};
        for (const auto &p : pairs) {
            const RateSchedule &withholding = t.federalWithholding().standard.value(p.w4);
            const RateSchedule &brackets = t.estimatedTax().rateSchedules.value(p.filing);
            const Money deduction = t.estimatedTax().standardDeduction.value(p.filing);
            const Money line1g = p.w4 == W4Status::MarriedJointly ? t.federalWithholding().step2UncheckedMarriedJointly
                                                                  : t.federalWithholding().step2UncheckedOther;
            QCOMPARE(deduction - line1g, p.shift);
            QCOMPARE(withholding.size(), brackets.size() + 1); // the 0 % row first
            for (qsizetype i = 0; i < brackets.size(); ++i) {
                QCOMPARE(withholding.at(i + 1).over, brackets.at(i).over + p.shift);
                QCOMPARE(withholding.at(i + 1).base, brackets.at(i).base);
                QCOMPARE(withholding.at(i + 1).rate, brackets.at(i).rate);
            }
        }
        for (const FilingStatus s : allFilingStatuses()) {
            QVERIFY2(inconsistency(t.estimatedTax().rateSchedules.value(s)).isEmpty(), qPrintable(filingStatusKey(s)));
        }
    }

    void aYearWithoutTablesIsRefused()
    {
        QString error;
        QVERIFY(!PayrollTables::forYear(2027, &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("no 2027 payroll tax tables")), qPrintable(error));
        QVERIFY(!PayrollTables::forYear(2025, &error).has_value());
    }

    void aDamagedFileIsRefusedWithThePath()
    {
        const QJsonObject good = QJsonDocument::fromJson(bundled2026()).object();
        QVERIFY(!good.isEmpty());
        QString error;
        QVERIFY(PayrollTables::fromJson(bundled2026(), &error).has_value());

        QJsonObject wrongSchema = good;
        wrongSchema.insert(QStringLiteral("schema"), 99);
        QVERIFY(!PayrollTables::fromJson(QJsonDocument(wrongSchema).toJson(), &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("schema 99")), qPrintable(error));

        QJsonObject noBase = good;
        QJsonObject federal = noBase.value(QStringLiteral("federal")).toObject();
        QJsonObject fica = federal.value(QStringLiteral("fica")).toObject();
        fica.remove(QStringLiteral("socialSecurityWageBase"));
        federal.insert(QStringLiteral("fica"), fica);
        noBase.insert(QStringLiteral("federal"), federal);
        QVERIFY(!PayrollTables::fromJson(QJsonDocument(noBase).toJson(), &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("federal.fica.socialSecurityWageBase")), qPrintable(error));

        // A float-looking rate is still text; a number is refused.
        fica = good.value(QStringLiteral("federal")).toObject().value(QStringLiteral("fica")).toObject();
        fica.insert(QStringLiteral("medicareRate"), 1.45);
        federal = good.value(QStringLiteral("federal")).toObject();
        federal.insert(QStringLiteral("fica"), fica);
        QJsonObject numeric = good;
        numeric.insert(QStringLiteral("federal"), federal);
        QVERIFY(!PayrollTables::fromJson(QJsonDocument(numeric).toJson(), &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("medicareRate")), qPrintable(error));
    }

    void unconfirmedValuesAreListed()
    {
        for (const UnconfirmedValue &u : t.unconfirmed()) {
            QVERIFY2(!u.label.isEmpty() && !u.note.isEmpty(), qPrintable(u.path));
            QVERIFY(!t.isConfirmed(u.path));
        }
        QVERIFY(t.isConfirmed(QStringLiteral("federal.fica.socialSecurityWageBase")));
        QVERIFY(!t.isConfirmed(QStringLiteral("colorado.withholding.before2020W4")));
        QVERIFY(!t.isConfirmed(QStringLiteral("colorado.opt.glendale")));  // a prefix touches its values
        QVERIFY(t.isConfirmed(QStringLiteral("colorado.opt.denver")));
        // Colorado cannot be a 2026 FUTA credit-reduction state (no Title XII
        // advance on January 1, 2026); the final DOL list comes in November.
        QVERIFY(t.futa().creditReduction.isEmpty());
        QVERIFY(t.futa().creditReductionNote.contains(QStringLiteral("November 10, 2026")));
    }

    void holidaysMoveDueDates()
    {
        const BusinessCalendar federal(t.federalHolidays());
        QVERIFY(federal.isHoliday(QDate(2026, 4, 16)));  // DC Emancipation Day
        QVERIFY(federal.isHoliday(QDate(2026, 7, 3)));   // Independence Day (observed)
        QVERIFY(federal.isHoliday(QDate(2027, 1, 1)));
        QVERIFY(!federal.isBusinessDay(QDate(2026, 10, 31))); // a Saturday
    }

    void noFloatingPointInBooksPayroll()
    {
        const QRegularExpression forbidden(QStringLiteral("\\b(double|float|qreal)\\b"));
        QDirIterator it(QStringLiteral(SOURCE_DIR "/../../libs/books_payroll/src"),
                        {QStringLiteral("*.h"), QStringLiteral("*.cpp")}, QDir::Files);
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
        QVERIFY(scanned >= 10);
    }
};

QTEST_GUILESS_MAIN(TstPayrollTables)
#include "tst_payroll_tables.moc"
