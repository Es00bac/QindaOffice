// SPDX-License-Identifier: GPL-3.0-or-later
// D-018 QindaWrite typography acceptance over the QtQuick app: highlight,
// super/subscript, line spacing, the insertions a word processor is
// reached for, and the word count readout.

#include "write_test_support.h"

#include "qindawrite/src/write_typography.h"

#include "standard_actions.h"

#include <QTextBlock>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace ActionId = QindaOffice::Quick::ActionId;

namespace {
QString sid(const char *id)
{
    return QString::fromLatin1(id);
}
} // namespace

class WriteTypographyTest : public QObject {
    Q_OBJECT
private slots:
    void fontSizeComboNeverGoesBlank()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // A pristine document carries no explicit point size on its char
        // format, so the combo used to show an empty box.
        bool ok = false;
        QVERIFY2(scene.host.fontSize().toInt(&ok) > 0 && ok, qPrintable(scene.host.fontSize()));
        QQuickItem *sizeCombo = nullptr;
        QTRY_VERIFY((sizeCombo = scene.item(QStringLiteral("fontSizeCombo"))) != nullptr);
        QTRY_VERIFY(!sizeCombo->property("currentText").toString().isEmpty());
    }

    void highlightAndScriptsApplyToTheCursor()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        PageTextControl *control = scene.control();
        QTextCursor cursor = control->textCursor();
        cursor.insertText(QStringLiteral("x2"));
        cursor.setPosition(1);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        control->setTextCursor(cursor);
        scene.host.actions()->trigger(sid(ActionId::kFormatSuperscript));
        QTextCursor probe(scene.doc->textDocument());
        probe.setPosition(1);
        probe.setPosition(2, QTextCursor::KeepAnchor);
        QCOMPARE(probe.charFormat().verticalAlignment(), QTextCharFormat::AlignSuperScript);
        QVERIFY(scene.doc->isDirty());
        scene.host.applyHighlightColor(QColor(Qt::yellow));
        QCOMPARE(probe.charFormat().background().color(), QColor(Qt::yellow));
    }

    void lineSpacingRewritesTheBlockFormat()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.control()->insertText(QStringLiteral("a paragraph"));
        scene.host.actions()->trigger(sid(ActionId::kFormatLineSpacingDouble));
        const QTextBlockFormat block = scene.control()->textCursor().blockFormat();
        QCOMPARE(block.lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
        QCOMPARE(qRound(block.lineHeight()), 200);
        // The menu's check mark must follow the document, not the last click.
        QVERIFY(scene.host.action(sid(ActionId::kFormatLineSpacingDouble))->isChecked());
        QVERIFY(!scene.host.action(sid(ActionId::kFormatLineSpacingSingle))->isChecked());
    }

    void wordCountFollowsTheDocumentAndSelection()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.control()->insertText(QStringLiteral("one two three four"));
        QCOMPARE(scene.host.typography()->counts().words, 4);
        QVERIFY2(scene.host.statusInfo().contains(QStringLiteral("4 words")), qPrintable(scene.host.statusInfo()));
        QTextCursor selection = scene.control()->textCursor();
        selection.setPosition(0);
        selection.setPosition(7, QTextCursor::KeepAnchor); // "one two"
        scene.control()->setTextCursor(selection);
        const WriteTypography::Counts counts = scene.host.typography()->counts();
        QVERIFY(counts.selectionOnly);
        QCOMPARE(counts.words, 2);
        QSignalSpy asked(&scene.host, &WriteHost::wordCountRequested);
        scene.host.actions()->trigger(sid(ActionId::kToolsWordCount));
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.first().first().toMap().value(QStringLiteral("words")).toInt(), 2);
    }

    void insertDateRuleSymbolAndLinkLandInTheDocument()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        const int blocksBefore = scene.doc->textDocument()->blockCount();
        scene.host.actions()->trigger(sid(ActionId::kInsertDate));
        QVERIFY(!scene.doc->textDocument()->toPlainText().isEmpty());
        scene.host.actions()->trigger(sid(ActionId::kInsertHorizontalRule));
        // The rule is a block with a trailing ruler, not a drawn line, so
        // it survives the HTML round trip like the rest of the document.
        bool foundRule = false;
        for (QTextBlock block = scene.doc->textDocument()->begin(); block.isValid(); block = block.next()) {
            if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
                foundRule = true;
            }
        }
        QVERIFY(foundRule);
        QVERIFY(scene.doc->textDocument()->blockCount() > blocksBefore);
        scene.host.insertSymbol(QStringLiteral("—"));
        QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("—")));
        scene.host.insertHyperlink(QStringLiteral("https://example.org"), QStringLiteral("Example"));
        QTextCursor probe = scene.control()->textCursor();
        probe.movePosition(QTextCursor::PreviousCharacter);
        QCOMPARE(probe.charFormat().anchorHref(), QStringLiteral("https://example.org"));
        QVERIFY(scene.doc->textDocument()->toPlainText().endsWith(QStringLiteral("Example")));
        // The link format does not bleed into what is typed next.
        QVERIFY(!scene.control()->currentCharFormat().isAnchor());
    }
};

WRITE_TEST_MAIN(WriteTypographyTest)

#include "tst_write_typography.moc"
