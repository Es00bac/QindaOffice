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
#include <QThread>
#include <QPixmap>
#include <QtTest>

#include <cmath>
#include <functional>

using namespace QindaOffice;
using namespace QindaOffice::Math;

class MathLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void fontHasMathMetrics();
    void fractionsScriptsAndStretch();
    void matrixColumnsAndCaret();
    void imagesAndSampleSheets();
    void transformedGlyphsAndWorkerLayouts();
    void stretchedBracketsHaveContinuousInk();
    void styledPowerPointRunsRetainInkAtHighDpi();
};

void MathLayoutTest::styledPowerPointRunsRetainInkAtHighDpi()
{
    std::optional<Equation> equation;
    for (const auto &sample : Test::mathSamples())
        if (QString::fromUtf8(sample.name) == u"powerpoint") equation = readOmmlFragment(Test::wordShaped(sample.xml));
    QVERIFY(equation);
    LayoutOptions options; options.fontSizePt = 18; options.placeholders = false;
    const EquationLayout layout(*equation, options);
    // The middle atom is the 28-point DrawingML-styled plus between a² and b.
    // The old QGlyphRun renderer silently lost this whole run at DPR 2.
    const qreal left = layout.caretRect({0, {}, 1}).x();
    const qreal right = layout.caretRect({0, {}, 2}).x();
    QVERIFY(right > left + 2);
    for (const int dpr : {1, 2, 3}) {
        const auto rendered = renderEquation(*equation, options, dpr);
        QVERIFY(!rendered.image.isNull());
        const int x0 = qCeil((rendered.origin.x() + left + 1) * dpr);
        const int x1 = qFloor((rendered.origin.x() + right - 1) * dpr);
        int ink = 0;
        for (int y = 0; y < rendered.image.height(); ++y) for (int x = x0; x < x1; ++x) {
            const QRgb pixel = rendered.image.pixel(x, y);
            if (qAlpha(pixel) > 128 && qGray(pixel) < 128) ++ink;
        }
        QVERIFY2(ink > 8 * dpr * dpr, qPrintable(QStringLiteral("Styled plus has no ink at DPR %1").arg(dpr)));
        const QString directory = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QImage paper(rendered.image.size(), QImage::Format_ARGB32_Premultiplied);
            paper.setDevicePixelRatio(dpr); paper.fill(Qt::white);
            QPainter painter(&paper); painter.drawImage(QPointF(0, 0), rendered.image); painter.end();
            QVERIFY(paper.save(directory + QStringLiteral("/office_math_powerpoint_%1x.png").arg(dpr)));
        }
    }
}

void MathLayoutTest::stretchedBracketsHaveContinuousInk()
{
    const auto equation = fromLatex(QStringLiteral(R"(\left[\begin{matrix}a&b\\c&d\\e&f\\g&h\end{matrix}\right])"), true);
    QVERIFY(equation);
    LayoutOptions options; options.fontSizePt = 18;
    const EquationLayout layout(*equation, options);
    QVERIFY(layout.isValid());
    int brackets = 0;
    std::function<void(const Box &)> check = [&](const Box &box) {
        if (box.type == Box::Type::Glyphs && box.glyphs.size() > 1) {
            ++brackets;
            const QRectF ink = box.inkBounds();
            constexpr int scale = 8;
            QImage image(qCeil((ink.width() + 8) * scale), qCeil((ink.height() + 8) * scale), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image); painter.scale(scale, scale);
            box.paint(painter, QPointF(4 - ink.left(), 4 - ink.top())); painter.end();
            // Every row through a square bracket's stem must contain ink.
            // A path's odd-even rule cancels overlapping extender outlines,
            // making visible horizontal holes at OpenType assembly seams.
            for (int y = scale * 4 + 1; y < qFloor((ink.height() + 4) * scale) - 1; ++y) {
                bool painted = false;
                for (int x = 0; x < image.width(); ++x) painted |= qGray(image.pixel(x, y)) < 128;
                QVERIFY2(painted, qPrintable(QStringLiteral("Bracket has an unpainted row at %1").arg(y)));
            }
        }
        for (const auto &child : box.children) check(child);
    };
    check(layout.box());
    QCOMPARE(brackets, 2);
}

void MathLayoutTest::transformedGlyphsAndWorkerLayouts()
{
    const auto equation = *fromLinear(QStringLiteral("x^2+a/b"));
    LayoutOptions options; options.fontSizePt = 24; options.dpi = 72;
    // Warm GUI-thread caches before the worker constructs its own fonts.
    const EquationLayout gui(equation, options);
    QVERIFY(gui.isValid());
    EquationLayout worker;
    QImage workerImage;
    std::unique_ptr<QThread> thread(QThread::create([&] {
        worker = EquationLayout(equation, options);
        workerImage = renderEquation(equation, options).image;
    }));
    thread->start(); QVERIFY(thread->wait(10000));
    QVERIFY(worker.isValid()); QVERIFY(!workerImage.isNull());
    QCOMPARE(worker.width(), gui.width());
    QCOMPARE(worker.inkBounds(), gui.inkBounds());
    // Paint a worker-made immutable layout after that thread and its raw
    // font caches have gone away, enlarged like a Show canvas equation.
    thread.reset();
    const auto draw = [](const EquationLayout &layout) {
        QPixmap pixmap(640, 320); pixmap.fill(Qt::white);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(30, 140); painter.scale(4, 4);
        layout.paint(painter, {}); painter.end(); return pixmap.toImage();
    };
    const auto expected = draw(gui), actual = draw(worker);
    QCOMPARE(actual, expected);
    int inkLeft = 0;
    for (int y = 0; y < actual.height(); ++y) for (int x = 30; x < 180; ++x)
        if (qGray(actual.pixel(x, y)) < 96) ++inkLeft;
    QVERIFY2(inkLeft > 100, "The x and superscript must paint, not just the fraction rule");
}

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
