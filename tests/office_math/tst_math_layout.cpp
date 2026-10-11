// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_samples.h"
#include "math_font.h"
#include "math_latex.h"
#include "math_layout.h"
#include "math_linear.h"
#include "math_omml.h"

#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QtTest>

#include <cmath>

using namespace QindaOffice;
using namespace QindaOffice::Math;

class MathLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void fontHasMathMetrics();
    void fractionsScriptsAndStretch();
    void matrixColumnsAndCaret();
    void imagesAndSampleSheets();
};

void MathLayoutTest::fontHasMathMetrics()
{
    const auto font = MathFont::open(QStringLiteral("Cambria Math"));
    QVERIFY2(font.isValid(), "An installed OpenType MATH font is required; package it through Portage");
    QVERIFY(font.unitsPerEm() > 0);
    QVERIFY(font.constant(MathConstant::FractionRuleThickness, 24) > 0);
    QVERIFY(font.constant(MathConstant::ScriptPercentScaleDown, 24) > 0);
    QVERIFY(!font.raw(24).fontTable("MATH").isEmpty());
}

void MathLayoutTest::fractionsScriptsAndStretch()
{
    LayoutOptions options;
    options.fontSizePt = 18;
    const auto fraction = fromLatex(QStringLiteral(R"(\frac{x}{y})"), true);
    const auto letter = fromLinear(QStringLiteral("x"));
    QVERIFY(fraction && letter);
    const EquationLayout full(*fraction, options);
    const EquationLayout simple(*letter, options);
    QVERIFY(full.isValid());
    QVERIFY(full.ascent() + full.descent() > 1.5 * (simple.ascent() + simple.descent()));
    const auto numerator = full.caretRect({0, {{0, 0}}, 0});
    const auto denominator = full.caretRect({0, {{0, 1}}, 0});
    QVERIFY(numerator.center().y() < denominator.center().y());
    auto inlineOptions = options;
    inlineOptions.display = false;
    const EquationLayout small(*fraction, inlineOptions);
    QVERIFY(small.ascent() + small.descent() < full.ascent() + full.descent());

    const auto scripts = fromLinear(QStringLiteral("x_i^2"));
    QVERIFY(scripts.has_value());
    const EquationLayout scriptLayout(*scripts, options);
    const auto base = scriptLayout.caretRect({0, {{0, 0}}, 0});
    const auto sub = scriptLayout.caretRect({0, {{0, 1}}, 0});
    const auto sup = scriptLayout.caretRect({0, {{0, 2}}, 0});
    QVERIFY(sub.center().y() > base.center().y());
    QVERIFY(sup.center().y() < base.center().y());
    const auto nested = fromLatex(QStringLiteral(R"(\left(\frac{a}{\frac{b}{c}}\right))"), true);
    const auto shortBracket = fromLatex(QStringLiteral(R"(\left(x\right))"), true);
    QVERIFY(nested && shortBracket);
    const EquationLayout tall(*nested, options), shortOne(*shortBracket, options);
    QVERIFY(tall.ascent() + tall.descent() > 2 * (shortOne.ascent() + shortOne.descent()));
}

void MathLayoutTest::matrixColumnsAndCaret()
{
    const auto matrix = fromLinear(QStringLiteral("■(a&b@c&d)"));
    QVERIFY(matrix.has_value());
    const EquationLayout layout(*matrix, {});
    QVERIFY(layout.isValid());
    const auto a = layout.caretRect({0, {{0, 0}}, 0});
    const auto b = layout.caretRect({0, {{0, 1}}, 0});
    const auto c = layout.caretRect({0, {{0, 2}}, 0});
    const auto d = layout.caretRect({0, {{0, 3}}, 0});
    QVERIFY(a.center().y() < c.center().y());
    QVERIFY(a.x() < b.x());
    QVERIFY(c.x() < d.x());
    // Equal-sized cells align in columns despite glyph-specific bearings.
    QVERIFY(qAbs(a.x() - c.x()) < 3);
    QVERIFY(qAbs(b.x() - d.x()) < 3);
    for (const auto &slot : layout.slotGeometries()) {
        if (slot.slot.steps.isEmpty() || slot.stops.size() < 2) {
            continue;
        }
        auto p = slot.slot;
        p.offset = 0;
        const auto hit = layout.positionAt(layout.caretRect(p).center());
        QCOMPARE(hit, p);
    }
}

void MathLayoutTest::imagesAndSampleSheets()
{
    const QString directory = qEnvironmentVariable("QQO_GRAB_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
    }
    LayoutOptions options;
    options.fontSizePt = 18;
    options.placeholders = false;
    for (const int dpr : {1, 2}) {
        QImage sheet(QSize(1400, 3000) * dpr, QImage::Format_ARGB32_Premultiplied);
        sheet.setDevicePixelRatio(dpr);
        sheet.fill(Qt::white);
        QPainter painter(&sheet);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::black);
        int row = 0;
        for (const auto &sample : Test::mathSamples()) {
            const auto equation = readOmmlFragment(Test::wordShaped(sample.xml));
            QVERIFY2(equation.has_value(), sample.name);
            const EquationLayout layout(*equation, options);
            QVERIFY2(layout.isValid(), sample.name);
            QVERIFY2(std::isfinite(layout.width()) && layout.width() > 0, sample.name);
            QVERIFY2(std::isfinite(layout.ascent()) && layout.ascent() > 0, sample.name);
            const auto rendered = renderEquation(*equation, options, dpr);
            QVERIFY(!rendered.image.isNull());
            QCOMPARE(rendered.image.devicePixelRatio(), qreal(dpr));
            QCOMPARE(rendered.width, layout.width());
            QVERIFY(rendered.origin.y() >= layout.ascent());
            const int column = row % 2;
            const qreal top = 20 + (row / 2) * 265;
            painter.drawText(QPointF(20 + column * 700, top + 16), QString::fromUtf8(sample.name));
            layout.paint(painter, QPointF(20 + column * 700, top + 65 + layout.ascent()));
            ++row;
        }
        painter.end();
        if (!directory.isEmpty()) {
            QVERIFY(sheet.save(directory + QStringLiteral("/office_math_%1x.png").arg(dpr)));
        }
    }
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    MathLayoutTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_math_layout.moc"
