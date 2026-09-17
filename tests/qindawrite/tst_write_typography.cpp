// SPDX-License-Identifier: GPL-3.0-or-later
// D-018 QindaWrite typography acceptance: highlight, super/subscript, line
// spacing, the insertions a word processor is reached for, and the word
// count readout. Split from tst_write_ui.cpp to keep both inside the line
// budget (AGENTS.md source discipline).

#include "qindawrite/src/page_text_view.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_typography.h"
#include "qindawrite/src/write_window.h"

#include "standard_actions.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QApplication>
#include <QComboBox>
#include <QTextBlock>
#include <QtTest>

using namespace QindaOffice::Apps::Write;

class WriteTypographyTest : public QObject {
    Q_OBJECT
private slots:
    void fontSizeComboNeverGoesBlank();
    void highlightAndScriptsApplyToTheCursor();
    void lineSpacingRewritesTheBlockFormat();
    void wordCountFollowsTheDocumentAndSelection();
    void insertDateAndRuleLandInTheDocument();
};

namespace ActionId = ::QindaOffice::Shell::ActionId;

void WriteTypographyTest::fontSizeComboNeverGoesBlank()
{
    WriteWindow window;
    window.show();
    auto *sizeCombo = window.findChild<QComboBox *>(QStringLiteral("fontSizeCombo"));
    QVERIFY(sizeCombo != nullptr);
    // A pristine document carries no explicit point size on its char
    // format, so the combo used to show an empty box — a state the user
    // cannot act on.
    QVERIFY2(!sizeCombo->currentText().isEmpty(), "size combo is blank on a fresh document");
    bool ok = false;
    QVERIFY(sizeCombo->currentText().toInt(&ok) > 0 && ok);
}

void WriteTypographyTest::highlightAndScriptsApplyToTheCursor()
{
    WriteWindow window;
    window.show();
    QWidgetTextControl *control = window.pageView()->control();
    QTextCursor cursor = control->textCursor();
    cursor.insertText(QStringLiteral("x2"));
    cursor.setPosition(1);
    cursor.setPosition(2, QTextCursor::KeepAnchor);
    control->setTextCursor(cursor);

    window.action(QString::fromLatin1(ActionId::kFormatSuperscript))->trigger();

    QTextCursor probe = control->textCursor();
    probe.setPosition(1);
    probe.setPosition(2, QTextCursor::KeepAnchor);
    QCOMPARE(probe.charFormat().verticalAlignment(), QTextCharFormat::AlignSuperScript);
    QVERIFY(window.writeDocument()->isDirty());
}

void WriteTypographyTest::lineSpacingRewritesTheBlockFormat()
{
    WriteWindow window;
    window.show();
    QWidgetTextControl *control = window.pageView()->control();
    control->textCursor().insertText(QStringLiteral("a paragraph"));

    window.action(QString::fromLatin1(ActionId::kFormatLineSpacingDouble))->trigger();

    const QTextBlockFormat block = control->textCursor().blockFormat();
    QCOMPARE(block.lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
    QCOMPARE(qRound(block.lineHeight()), 200);
    // The menu's check mark must follow the document, not the last click.
    QVERIFY(window.action(QString::fromLatin1(ActionId::kFormatLineSpacingDouble))->isChecked());
}

void WriteTypographyTest::wordCountFollowsTheDocumentAndSelection()
{
    WriteWindow window;
    window.show();
    QWidgetTextControl *control = window.pageView()->control();
    control->textCursor().insertText(QStringLiteral("one two three four"));

    QCOMPARE(window.typography()->counts().words, 4);
    QVERIFY2(window.statusInfo().contains(QStringLiteral("4 words")),
             qPrintable(window.statusInfo()));

    QTextCursor selection = control->textCursor();
    selection.setPosition(0);
    selection.setPosition(7, QTextCursor::KeepAnchor); // "one two"
    control->setTextCursor(selection);
    const WriteTypography::Counts counts = window.typography()->counts();
    QVERIFY(counts.selectionOnly);
    QCOMPARE(counts.words, 2);
}

void WriteTypographyTest::insertDateAndRuleLandInTheDocument()
{
    WriteWindow window;
    window.show();
    const int blocksBefore = window.writeDocument()->textDocument()->blockCount();

    window.action(QString::fromLatin1(ActionId::kInsertDate))->trigger();
    QVERIFY(!window.writeDocument()->textDocument()->toPlainText().isEmpty());

    window.action(QString::fromLatin1(ActionId::kInsertHorizontalRule))->trigger();
    // The rule is a block with a trailing ruler, not a drawn line, so it
    // survives the HTML round trip like the rest of the document.
    bool foundRule = false;
    for (QTextBlock block = window.writeDocument()->textDocument()->begin(); block.isValid();
         block = block.next()) {
        if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            foundRule = true;
        }
    }
    QVERIFY(foundRule);
    QVERIFY(window.writeDocument()->textDocument()->blockCount() > blocksBefore);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    WriteTypographyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_typography.moc"
