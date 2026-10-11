// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_editor.h"
#include "math_linear.h"
#include "math_omml.h"
#include "support/ooxml_schema.h"

#include <QtTest>

using namespace QindaOffice::Math;

class MathEditorTest : public QObject {
    Q_OBJECT
private slots:
    void typingAndHistoryOwnTheirTrees();
    void selectionWrapsAndNavigates();
    void deletionUsesCodePoints();
    void invalidSelectionsLeaveHistoryAlone();
    void matrixAndHiddenSlotNavigation();
    void templatesRoundTrip();
    void unknownPropertiesSurviveEditing();
};

void MathEditorTest::typingAndHistoryOwnTheirTrees()
{
    auto source = fromLinear(QStringLiteral("ab"));
    QVERIFY(source.has_value());
    EquationEditor editor(*source);
    source->lines[0].items[0]->setText(QStringLiteral("unrelated"));
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("ab"));
    QVERIFY(editor.setPosition({0, {}, 1}));
    QSignalSpy changed(&editor, &EquationEditor::changed);
    QVERIFY(editor.insertText(QStringLiteral("x")));
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("axb"));
    QCOMPARE(changed.size(), 1);
    QVERIFY(editor.undo());
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("ab"));
    QCOMPARE(editor.position().offset, 1);
    QVERIFY(editor.redo());
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("axb"));
    QVERIFY(editor.undo());
    QVERIFY(editor.insertText(QStringLiteral("c")));
    QVERIFY(!editor.canRedo());
    editor.reset(*source);
    QVERIFY(!editor.canUndo());
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("unrelated"));
}

void MathEditorTest::selectionWrapsAndNavigates()
{
    EquationEditor editor;
    QVERIFY(editor.insertText(QStringLiteral("a+b")));
    QVERIFY(editor.setPosition({0, {}, 0}));
    QVERIFY(editor.setPosition({0, {}, 3}, true));
    auto fraction = Structure::fraction();
    QVERIFY(editor.insertStructure(fraction));
    QCOMPARE(editor.position(), (MathPosition{0, {{0, 0}}, 3}));
    fraction.node->args[0].items.append(makeRun(QStringLiteral("external")));
    QCOMPARE(plainText(editor.equation().lines[0].items[0]->args[0]), QStringLiteral("a+b"));
    QVERIFY(editor.moveVertical(false));
    QCOMPARE(editor.position(), (MathPosition{0, {{0, 1}}, 0}));
    QVERIFY(editor.insertText(QStringLiteral("c")));
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("(a+b)/c"));
    QVERIFY(editor.moveHorizontal(true));
    QCOMPARE(editor.position(), (MathPosition{0, {}, 1}));
    QVERIFY(editor.eraseBackward());
    QVERIFY(editor.equation().isEmpty());
    QVERIFY(editor.undo());
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("(a+b)/c"));
    QVERIFY(!editor.hasSelection());
    QVERIFY(editor.undo());
    QVERIFY(editor.undo());
    QCOMPARE(toLinear(editor.equation()), QStringLiteral("a+b"));
    QVERIFY(editor.hasSelection());
}

void MathEditorTest::deletionUsesCodePoints()
{
    EquationEditor editor;
    const QString pair = QString::fromUcs4(U"a𝛼b");
    QVERIFY(editor.insertText(pair));
    QCOMPARE(editor.position().offset, 3);
    QVERIFY(editor.setPosition({0, {}, 2}));
    QVERIFY(editor.eraseBackward());
    QCOMPARE(plainText(editor.equation().lines[0]), QStringLiteral("ab"));
    QCOMPARE(editor.position().offset, 1);
    QVERIFY(editor.undo());
    QCOMPARE(plainText(editor.equation().lines[0]), pair);
    QVERIFY(editor.setPosition({0, {}, 1}));
    QVERIFY(editor.eraseForward());
    QCOMPARE(plainText(editor.equation().lines[0]), QStringLiteral("ab"));
    QVERIFY(editor.setPosition({0, {}, 0}));
    QVERIFY(!editor.eraseBackward());
}

void MathEditorTest::invalidSelectionsLeaveHistoryAlone()
{
    EquationEditor editor;
    QVERIFY(!editor.setPosition({1, {}, 0}));
    QVERIFY(!editor.setPosition({0, {{99, 0}}, 0}));
    QVERIFY(!editor.setPosition({0, {}, -1}));
    QVERIFY(!editor.insertText(QString()));
    QVERIFY(!editor.insertStructure({}));
    QVERIFY(!editor.canUndo());
    QVERIFY(editor.insertStructure(Structure::fraction()));
    QVERIFY(!editor.setPosition({0, {{0, 1}}, 0}, true));
    QCOMPARE(editor.position(), (MathPosition{0, {{0, 0}}, 0}));
    QVERIFY(editor.undo());
    QVERIFY(!editor.canUndo());
}

void MathEditorTest::matrixAndHiddenSlotNavigation()
{
    EquationEditor editor;
    QVERIFY(editor.insertStructure(Structure::matrix(2, 3)));
    QVERIFY(editor.nextSlot());
    QCOMPARE(editor.position().steps.last().arg, 1);
    QVERIFY(editor.moveVertical(false));
    QCOMPARE(editor.position().steps.last().arg, 4);
    QVERIFY(editor.moveVertical(true));
    QCOMPARE(editor.position().steps.last().arg, 1);
    QVERIFY(!editor.moveVertical(true));
    QCOMPARE(navigableSlots(*Structure::radical().node), QList<int>{1});
    QCOMPARE(navigableSlots(*Structure::integral(U'∫', false).node), QList<int>{2});
    QCOMPARE(navigableSlots(*Structure::largeOperator(U'∑', true, false).node), (QList<int>{0, 2}));
}

void MathEditorTest::templatesRoundTrip()
{
    const QList<Structure> templates = {
        Structure::fraction(), Structure::fraction(FractionType::NoBar), Structure::superscript(),
        Structure::subscript(), Structure::subSuperscript(), Structure::leftSubSuperscript(),
        Structure::radical(), Structure::radical(true), Structure::integral(), Structure::largeOperator(),
        Structure::bracket(), Structure::function(), Structure::accent(), Structure::bar(),
        Structure::groupCharacter(), Structure::limit(), Structure::logarithm(), Structure::operatorWithText(),
        Structure::matrix(), Structure::equationArray(), Structure::boxed(), Structure::box(), Structure::phantom()};
    for (const auto &structure : templates) {
        EquationEditor editor;
        QVERIFY(editor.insertStructure(structure));
        QVERIFY(editor.insertText(QStringLiteral("x")));
        const auto xml = writeOmmlFragment(editor.equation());
        const auto back = readOmmlFragment(xml);
        QVERIFY2(back.has_value(), qPrintable(xml));
        QCOMPARE(writeOmmlFragment(*back), xml);
        if (QindaOffice::Test::schemaValidationAvailable()) {
            const auto errors = QindaOffice::Test::validateXmlPart(
                QindaOffice::Ooxml::serializeDocument(*writeOmml(editor.equation())), QStringLiteral("shared-math.xsd"));
            QVERIFY2(errors.isEmpty(), qPrintable(errors));
        }
        QVERIFY(editor.undo());
        QVERIFY(editor.undo());
        QVERIFY(editor.equation().isEmpty());
    }
}

void MathEditorTest::unknownPropertiesSurviveEditing()
{
    auto original = readOmmlFragment(QStringLiteral(
        "<m:oMath xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\" xmlns:x=\"urn:later\">"
        "<m:r x:tag=\"kept\"><m:rPr><m:sty m:val=\"b\"/><x:later/></m:rPr><m:t>ab</m:t></m:r></m:oMath>"));
    QVERIFY(original.has_value());
    EquationEditor editor(*original);
    QVERIFY(editor.setPosition({0, {}, 1}));
    QVERIFY(editor.insertText(QStringLiteral("x")));
    const auto written = writeOmmlFragment(editor.equation());
    QVERIFY(written.contains(QStringLiteral("later")));
    QVERIFY(written.contains(QStringLiteral("kept")));
    QCOMPARE(plainText(editor.equation().lines[0]), QStringLiteral("axb"));
    QCOMPARE(editor.equation().lines[0].items.size(), 1);
    QCOMPARE(written.count(QStringLiteral("kept")), 1);
    QCOMPARE(runStyle(*editor.equation().lines[0].items[0]), MathStyle::Bold);
    QVERIFY(editor.undo());
    QCOMPARE(writeOmmlFragment(editor.equation()), writeOmmlFragment(*original));
}

QTEST_GUILESS_MAIN(MathEditorTest)
#include "tst_math_editor.moc"
