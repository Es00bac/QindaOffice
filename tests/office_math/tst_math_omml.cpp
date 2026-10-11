// SPDX-License-Identifier: GPL-3.0-or-later
// M92 gates for OMML in and out (D-100): Word-shaped samples of every
// structure read into the model and written back EQUIVALENT (same tree as
// the source), the typed properties, what is kept verbatim (unknown
// elements and attributes, invalid structures), edits that stay valid, and
// shared-math.xsd validation of everything written (QQO_OOXML_SCHEMA_DIR).

#include "math_omml.h"
#include "math_props.h"
#include "math_samples.h"
#include "ooxml_namespaces.h"
#include "support/ooxml_schema.h"

#include <QMap>
#include <QtTest>

#include <functional>

using namespace QindaOffice;
using namespace QindaOffice::Math;
using Test::mathSamples;
using Test::wordShaped;

namespace {

Ooxml::XmlElementPtr parseSample(const char *xml)
{
    return Ooxml::parseXml(wordShaped(xml).toUtf8());
}

const Test::MathSample *sampleNamed(const QString &name)
{
    for (const Test::MathSample &s : mathSamples()) {
        if (name == QLatin1String(s.name)) {
            return &s;
        }
    }
    return nullptr;
}

Equation readSample(const QString &name)
{
    const Test::MathSample *s = sampleNamed(name);
    const Ooxml::XmlElementPtr root = s != nullptr ? parseSample(s->xml) : nullptr;
    const std::optional<Equation> eq = root ? readOmml(*root) : std::nullopt;
    return eq ? *eq : Equation();
}

void countKinds(const Argument &arg, QMap<int, int> &counts)
{
    for (const NodePtr &n : arg.items) {
        counts[int(n->kind)] += 1;
        for (const Argument &a : n->args) {
            countKinds(a, counts);
        }
    }
}

QMap<int, int> kindsOf(const Equation &eq)
{
    QMap<int, int> counts;
    for (const Argument &line : eq.lines) {
        countKinds(line, counts);
    }
    return counts;
}

// The first node of `kind` in reading order.
const Node *findKind(const Argument &arg, NodeKind kind)
{
    for (const NodePtr &n : arg.items) {
        if (n->kind == kind) {
            return n.get();
        }
        for (const Argument &a : n->args) {
            if (const Node *found = findKind(a, kind)) {
                return found;
            }
        }
    }
    return nullptr;
}

const Node *findKind(const Equation &eq, NodeKind kind, int skip = 0)
{
    QList<const Node *> all;
    std::function<void(const Argument &)> walk = [&](const Argument &arg) {
        for (const NodePtr &n : arg.items) {
            if (n->kind == kind) {
                all.append(n.get());
            }
            for (const Argument &a : n->args) {
                walk(a);
            }
        }
    };
    for (const Argument &line : eq.lines) {
        walk(line);
    }
    return skip < all.size() ? all.at(skip) : nullptr;
}

QString schemaErrors(const Ooxml::XmlElement &root)
{
    Ooxml::XmlElementPtr copy = root.clone();
    Test::detail::preprocess(*copy, {});
    return Test::validateXmlPart(Ooxml::serializeDocument(*copy), QStringLiteral("shared-math.xsd"));
}

} // namespace

class MathOmmlTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripsWordSamples_data();
    void roundTripsWordSamples();
    void coversEveryStructure();
    void validatesAgainstSharedMath_data();
    void validatesAgainstSharedMath();
    void readsTypedProperties();
    void editsStayValid();
    void keepsUnknownsThroughEdits();
    void keepsInvalidStructuresVerbatim();
    void readsMathSettings();
    void showsKeptContent();
};

void MathOmmlTest::roundTripsWordSamples_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("kept");
    for (const Test::MathSample &s : mathSamples()) {
        QTest::newRow(s.name) << QString::fromLatin1(s.name) << (QByteArray(s.name) == "unknowns" ? 6 : 0);
    }
}

void MathOmmlTest::roundTripsWordSamples()
{
    QFETCH(QString, name);
    QFETCH(int, kept);
    const Ooxml::XmlElementPtr root = parseSample(sampleNamed(name)->xml);
    QVERIFY(root);
    const std::optional<Equation> eq = readOmml(*root);
    QVERIFY(eq);
    QCOMPARE(kindsOf(*eq).value(int(NodeKind::Kept)), kept);
    // Unedited, the written element is the source element.
    const Ooxml::XmlElementPtr written = writeOmml(*eq);
    QCOMPARE(Ooxml::serializeFragment(*written), Ooxml::serializeFragment(*root));
    // The fragment form office_docx stores reads and writes the same way.
    const std::optional<Equation> again = readOmmlFragment(Ooxml::serializeFragment(*root));
    QVERIFY(again);
    QCOMPARE(writeOmmlFragment(*again), Ooxml::serializeFragment(*root));
    // A deep copy writes the same; mutating it leaves the original alone.
    Equation copy = eq->clone();
    if (!copy.lines.isEmpty() && !copy.lines.first().items.isEmpty()) {
        copy.lines.first().items.removeFirst();
    }
    QCOMPARE(writeOmmlFragment(*eq), Ooxml::serializeFragment(*root));
}

void MathOmmlTest::coversEveryStructure()
{
    QMap<int, int> total;
    for (const Test::MathSample &s : mathSamples()) {
        const QMap<int, int> counts = kindsOf(readSample(QString::fromLatin1(s.name)));
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            total[it.key()] += it.value();
        }
    }
    for (int k = int(NodeKind::Run); k <= int(NodeKind::Kept); ++k) {
        QVERIFY2(total.value(k) > 0, qPrintable(QStringLiteral("no sample has node kind %1").arg(k)));
    }
    const Equation quadratic = readSample(QStringLiteral("quadratic"));
    QVERIFY(quadratic.display);
    QCOMPARE(justification(quadratic, Justification::CenterGroup), Justification::Center);
    QVERIFY(!readSample(QStringLiteral("euler")).display);
}

void MathOmmlTest::validatesAgainstSharedMath_data()
{
    QTest::addColumn<QString>("name");
    for (const Test::MathSample &s : mathSamples()) {
        if (s.schemaValid) {
            QTest::newRow(s.name) << QString::fromLatin1(s.name);
        }
    }
}

void MathOmmlTest::validatesAgainstSharedMath()
{
    if (!Test::schemaValidationAvailable()) {
        QSKIP("QQO_OOXML_SCHEMA_DIR or xmllint unavailable");
    }
    QFETCH(QString, name);
    const Ooxml::XmlElementPtr written = writeOmml(readSample(name));
    const QString errors = schemaErrors(*written);
    QVERIFY2(errors.isEmpty(), qPrintable(errors));
}

void MathOmmlTest::readsTypedProperties()
{
    const Equation fr = readSample(QStringLiteral("fractions"));
    QCOMPARE(fractionType(*findKind(fr, NodeKind::Fraction, 0)), FractionType::Skewed);
    QCOMPARE(fractionType(*findKind(fr, NodeKind::Fraction, 1)), FractionType::Linear);
    QCOMPARE(fractionType(*findKind(fr, NodeKind::Fraction, 2)), FractionType::Bar);
    QCOMPARE(fractionType(*findKind(readSample(QStringLiteral("binomial")), NodeKind::Fraction)), FractionType::NoBar);

    const Equation ops = readSample(QStringLiteral("operators"));
    const Node &prod = *findKind(ops, NodeKind::Nary, 0);
    QCOMPARE(naryChar(prod), QStringLiteral("∏"));
    QCOMPARE(limitLocation(prod), LimitLocation::UnderOver);
    QVERIFY(supHidden(prod) && !subHidden(prod));
    const Node &grow = *findKind(ops, NodeKind::Nary, 2);
    QCOMPARE(naryChar(grow), QStringLiteral("∫"));
    QCOMPARE(limitLocation(grow), LimitLocation::Default);
    QVERIFY(naryGrows(grow));

    const Equation dl = readSample(QStringLiteral("delimiters"));
    const Node &braket = *findKind(dl, NodeKind::Delimiter, 0);
    QCOMPARE(delimiterBegin(braket), QStringLiteral("⟨"));
    QCOMPARE(delimiterSeparator(braket), QStringLiteral("|"));
    QCOMPARE(braket.args.size(), 2);
    QCOMPARE(delimiterShape(*findKind(dl, NodeKind::Delimiter, 1)), DelimiterShape::Match);
    QVERIFY(!delimiterGrows(*findKind(dl, NodeKind::Delimiter, 2)));
    const Node &plain = *findKind(dl, NodeKind::Delimiter, 3);
    QCOMPARE(delimiterBegin(plain), QStringLiteral("("));
    QCOMPARE(delimiterSeparator(plain), QStringLiteral("│"));
    QCOMPARE(delimiterEnd(plain), QStringLiteral(")"));
    QVERIFY(delimiterGrows(plain));
    QCOMPARE(delimiterEnd(*findKind(readSample(QStringLiteral("cases")), NodeKind::Delimiter, 1)), QString());

    const Equation acc = readSample(QStringLiteral("accents"));
    QCOMPARE(accentChar(*findKind(acc, NodeKind::Accent, 0)), QStringLiteral("\u0302"));
    QCOMPARE(accentChar(*findKind(acc, NodeKind::Accent, 2)), QStringLiteral("\u20D7"));
    QCOMPARE(barPosition(*findKind(acc, NodeKind::Bar, 0)), VerticalPosition::Top);
    QCOMPARE(barPosition(*findKind(acc, NodeKind::Bar, 1)), VerticalPosition::Bottom);
    QCOMPARE(Math::groupChar(*findKind(acc, NodeKind::GroupChar, 0)), QStringLiteral("⏞"));
    QCOMPARE(groupCharPosition(*findKind(acc, NodeKind::GroupChar, 0)), VerticalPosition::Top);
    QCOMPARE(Math::groupChar(*findKind(acc, NodeKind::GroupChar, 1)), QStringLiteral("⏟"));

    QVERIFY(degreeHidden(*findKind(readSample(QStringLiteral("quadratic")), NodeKind::Radical)));
    QVERIFY(!degreeHidden(*findKind(readSample(QStringLiteral("radicals")), NodeKind::Radical)));
    QVERIFY(alignedScripts(*findKind(readSample(QStringLiteral("scripts")), NodeKind::SubSup)));

    const Equation boxes = readSample(QStringLiteral("boxes"));
    QVERIFY(boxIsOperator(*findKind(boxes, NodeKind::Box)));
    QVERIFY(borderFlags(*findKind(boxes, NodeKind::BorderBox)).strikeUp);
    QVERIFY(!phantomFlags(*findKind(boxes, NodeKind::Phantom, 0)).show);
    const PhantomFlags smash = phantomFlags(*findKind(boxes, NodeKind::Phantom, 1));
    QVERIFY(smash.show && smash.zeroAscent && smash.zeroDescent && !smash.zeroWidth);
    QVERIFY(boxIsDifferential(*findKind(readSample(QStringLiteral("integral")), NodeKind::Box)));

    const Equation arrays = readSample(QStringLiteral("arrays"));
    const Node &arr = *findKind(arrays, NodeKind::EqArray);
    QCOMPARE(baseJustification(arr), VerticalJustification::Top);
    QCOMPARE(rowSpacingRule(arr), 1);
    const Node &m = *findKind(arrays, NodeKind::Matrix);
    QCOMPARE(baseJustification(m), VerticalJustification::Bottom);
    QCOMPARE(columnCount(m), 2);
    QCOMPARE(m.rowSizes, QList<int>({2, 2}));
    QCOMPARE(columnAlignment(m, 0), ColumnAlignment::Left);
    QCOMPARE(columnAlignment(m, 1), ColumnAlignment::Right);
    QVERIFY(placeholdersHidden(m));
    QCOMPARE(columnGapRule(m), 2);
    QCOMPARE(columnAlignment(*findKind(readSample(QStringLiteral("matrix3")), NodeKind::Matrix), 2),
             ColumnAlignment::Center);

    const Equation runsEquation = readSample(QStringLiteral("runs"));
    const Argument &runs = runsEquation.lines.first();
    const QList<MathStyle> styles{MathStyle::Plain, MathStyle::Bold, MathStyle::Italic, MathStyle::BoldItalic};
    for (int i = 0; i < styles.size(); ++i) {
        QCOMPARE(runStyle(*runs.items.at(i)), styles.at(i));
    }
    const QList<MathScript> scripts{MathScript::Script, MathScript::Fraktur, MathScript::DoubleStruck,
                                    MathScript::SansSerif, MathScript::Monospace};
    for (int i = 0; i < scripts.size(); ++i) {
        QCOMPARE(runScript(*runs.items.at(4 + i)), scripts.at(i));
    }
    QVERIFY(runIsNormalText(*runs.items.at(9)) && runIsLiteral(*runs.items.at(9)));
    QCOMPARE(runs.items.at(9)->text(), QStringLiteral(" text "));
    QCOMPARE(runs.items.at(10)->text(), QStringLiteral("xy"));
    QCOMPARE(runs.items.at(10)->pieces.size(), 2);
    QCOMPARE(runStyle(*runs.items.at(10)), MathStyle::Default);
}

void MathOmmlTest::editsStayValid()
{
    // Every structure built from scratch, properties set out of schema order.
    Equation eq = makeEquation(true);
    for (int k = int(NodeKind::Fraction); k < int(NodeKind::Kept); ++k) {
        NodePtr n = makeNode(NodeKind(k), 2, 2);
        for (Argument &a : n->args) {
            a.items.append(makeRun(QStringLiteral("x")));
        }
        eq.lines.first().items.append(n);
    }
    // Item k - 1 is the structure of NodeKind k.
    Node &d = *eq.lines.first().items.at(int(NodeKind::Delimiter) - 1);
    QCOMPARE(d.kind, NodeKind::Delimiter);
    setPropValue(d, u"shp", QStringLiteral("match"));
    setPropValue(d, u"endChr", QStringLiteral("]"));
    setPropOn(d, u"grow", false);
    setPropValue(d, u"begChr", QStringLiteral("["));
    QCOMPARE(delimiterBegin(d), QStringLiteral("["));
    QCOMPARE(delimiterEnd(d), QStringLiteral("]"));
    QVERIFY(!delimiterGrows(d));
    QStringList order;
    for (const Ooxml::XmlElement *c : d.props->elements()) {
        order.append(c->name);
    }
    QCOMPARE(order, QStringList({QStringLiteral("begChr"), QStringLiteral("endChr"), QStringLiteral("grow"),
                                 QStringLiteral("shp")}));
    Node &f = *eq.lines.first().items.at(0);
    setFractionType(f, FractionType::Skewed);
    QCOMPARE(fractionType(f), FractionType::Skewed);
    Node &run = *d.args.first().items.first();
    setRunStyle(run, MathStyle::Bold);
    setRunScript(run, MathScript::DoubleStruck);
    setRunNormalText(run, true);
    QVERIFY(runIsNormalText(run));
    QCOMPARE(runStyle(run), MathStyle::Default); // m:nor replaced the style (a schema choice)
    setRunStyle(run, MathStyle::BoldItalic);
    QVERIFY(!runIsNormalText(run));
    setJustification(eq, Justification::Left);
    QCOMPARE(justification(eq, Justification::CenterGroup), Justification::Left);
    // The written equation reads back to the same element and validates.
    const Ooxml::XmlElementPtr written = writeOmml(eq);
    const std::optional<Equation> back = readOmml(*written);
    QVERIFY(back);
    QCOMPARE(kindsOf(*back).value(int(NodeKind::Kept)), 0);
    QCOMPARE(writeOmmlFragment(*back), Ooxml::serializeFragment(*written));
    if (Test::schemaValidationAvailable()) {
        const QString errors = schemaErrors(*written);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
    }
    // A run whose text gains edge spaces is written with xml:space="preserve".
    run.setText(QStringLiteral(" spaced "));
    QVERIFY(writeOmmlFragment(eq).contains(QStringLiteral("xml:space=\"preserve\"> spaced </m:t>")));
}

void MathOmmlTest::keepsUnknownsThroughEdits()
{
    Equation eq = readSample(QStringLiteral("unknowns"));
    Argument &line = eq.lines.first();
    QCOMPARE(line.items.first()->kind, NodeKind::Kept);
    QCOMPARE(line.items.at(1)->attributes.size(), 1); // w14:paraId on the run
    line.items.at(1)->setText(QStringLiteral("alpha"));
    const Node *f = findKind(line, NodeKind::Fraction);
    QVERIFY(f && f->props);
    QCOMPARE(fractionType(*f), FractionType::Linear);
    const QString out = writeOmmlFragment(eq);
    for (const char *kept : {"w14:paraId=\"1A2B3C4D\"", "<w14:fancy w14:val=\"1\"/>", "<w14:later>kept</w14:later>",
                             "<w:bookmarkStart w:id=\"0\" w:name=\"eq1\"/>", "<w:proofErr w:type=\"spellStart\"/>",
                             "w:author=\"Reviewer\"", "mc:Ignorable=\"w14\"", "<m:t>alpha</m:t>"}) {
        QVERIFY2(out.contains(QString::fromUtf8(kept)), kept);
    }
    if (Test::schemaValidationAvailable()) {
        const QString errors = schemaErrors(*writeOmml(eq));
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
    }
}

void MathOmmlTest::keepsInvalidStructuresVerbatim()
{
    for (const char *xml : {"<m:oMath><m:f><m:num><m:r><m:t>1</m:t></m:r></m:num></m:f></m:oMath>",
                            "<m:oMath><m:f><m:den/><m:num/></m:f></m:oMath>",
                            "<m:oMath><m:m><m:mr><w:proofErr w:type=\"gramStart\"/></m:mr></m:m></m:oMath>",
                            "<m:oMath><m:futureStructure m:val=\"2\"><m:e><m:r><m:t>z</m:t></m:r></m:e>"
                            "</m:futureStructure></m:oMath>"}) {
        const Ooxml::XmlElementPtr root = parseSample(xml);
        QVERIFY2(root, xml);
        const std::optional<Equation> eq = readOmml(*root);
        QVERIFY(eq);
        QCOMPARE(eq->lines.first().items.first()->kind, NodeKind::Kept);
        QCOMPARE(writeOmmlFragment(*eq), Ooxml::serializeFragment(*root));
    }
    QVERIFY(!readOmml(Ooxml::XmlElement(QString::fromUtf16(Ooxml::Ns::W), QStringLiteral("p"))));
    QVERIFY(!readOmmlFragment(QStringLiteral("<not-xml")));
}

void MathOmmlTest::readsMathSettings()
{
    const Ooxml::XmlElementPtr pr = parseSample(
        "<m:mathPr><m:mathFont m:val=\"Cambria Math\"/><m:brkBin m:val=\"before\"/><m:smallFrac m:val=\"1\"/>"
        "<m:dispDef/><m:lMargin m:val=\"240\"/><m:rMargin m:val=\"0\"/><m:defJc m:val=\"left\"/>"
        "<m:wrapIndent m:val=\"1440\"/><m:intLim m:val=\"undOvr\"/><m:naryLim m:val=\"subSup\"/></m:mathPr>");
    QVERIFY(pr);
    const MathSettings s = MathSettings::fromXml(*pr);
    QCOMPARE(s.mathFont, QStringLiteral("Cambria Math"));
    QVERIFY(s.smallFraction);
    QVERIFY(s.displayDefaults);
    QCOMPARE(s.defaultJustification, Justification::Left);
    QCOMPARE(s.integralLimits, LimitLocation::UnderOver);
    QCOMPARE(s.naryLimits, LimitLocation::SubSup);
    QCOMPARE(s.leftMarginPt, 12.0);
    const MathSettings defaults;
    QCOMPARE(defaults.integralLimits, LimitLocation::SubSup);
    QCOMPARE(defaults.naryLimits, LimitLocation::UnderOver);
}

void MathOmmlTest::showsKeptContent()
{
    const Equation unknowns = readSample(QStringLiteral("unknowns"));
    const Argument &line = unknowns.lines.first();
    QVERIFY(!keptMath(*line.items.at(0))); // bookmarkStart shows nothing
    const std::optional<Argument> inserted = keptMath(*line.items.at(2));
    QVERIFY(inserted && inserted->items.size() == 1);
    QCOMPARE(inserted->items.first()->text(), QStringLiteral("+b"));
    const std::optional<Argument> deleted = keptMath(*line.items.at(5));
    QVERIFY(deleted && deleted->isEmpty());
    QCOMPARE(plainText(line), QStringLiteral("a+bcdkept−e"));
}

QTEST_MAIN(MathOmmlTest)
#include "tst_math_omml.moc"
