// SPDX-License-Identifier: GPL-3.0-or-later
// M11 UI completeness acceptance: the window exposes the main + formatting
// toolbars and app menus with persistent named actions; character and
// paragraph formatting round-trips between the actions, the document and
// the synced UI state; find/replace works; the page renders as paper
// (pinned palette) under any desktop theme.

#include "qindawrite/src/find_replace_bar.h"
#include "qindawrite/src/format_controller.h"
#include "qindawrite/src/page_setup_io.h"
#include "qindawrite/src/page_text_view.h"
#include "qindawrite/src/table_actions.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_window.h"

#include "standard_actions.h"

#include <QComboBox>
#include <QFile>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTextList>
#include <QTextTable>
#include <QToolBar>
#include <QtTest>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

using namespace QindaOffice::Apps::Write;

class WriteUiTest : public QObject {
    Q_OBJECT
private slots:
    void toolbarsExposeNamedActions();
    void appMenusExist();
    void charFormatActionsApplyAndSync();
    void fontControlsApplyAndSync();
    void alignmentAndListsApply();
    void indentChangesBlockFormat();
    void tableAndColumnsInsert();
    void findAndReplaceFlows();
    void editActionsFollowControlAvailability();
    void paperPaletteIsPinned();
    void zoomActionsAdjustView();
    void tableOperationsWork();
    void pageSetupRoundTripsThroughHtml();
};

namespace {

// Puts "hello world" into the document and selects "hello".
QTextCursor selectFirstWord(WriteWindow *window)
{
    QTextDocument *doc = window->writeDocument()->textDocument();
    QTextCursor cursor(doc);
    cursor.insertText(QStringLiteral("hello world"));
    QTextCursor selection(doc);
    selection.setPosition(0);
    selection.setPosition(5, QTextCursor::KeepAnchor);
    window->pageView()->control()->setTextCursor(selection);
    return selection;
}

} // namespace

void WriteUiTest::toolbarsExposeNamedActions()
{
    WriteWindow window;
    QToolBar *mainBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QToolBar *formatBar = window.findChild<QToolBar *>(QStringLiteral("formatToolBar"));
    QVERIFY(mainBar != nullptr);
    QVERIFY(formatBar != nullptr);

    namespace ActionId = ::QindaOffice::Shell::ActionId;
    const QList<const char *> mainIds = {ActionId::kFileNew, ActionId::kFileOpen,
                                         ActionId::kFileSave, ActionId::kEditUndo,
                                         ActionId::kEditCut, ActionId::kEditFind};
    for (const char *id : mainIds) {
        QAction *action = window.action(QString::fromLatin1(id));
        QVERIFY2(action != nullptr, id);
        QVERIFY2(mainBar->actions().contains(action), id);
    }

    const WriteActions &format = window.formatController()->actions();
    for (QAction *action : format.all()) {
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
    for (QAction *action : {format.bold, format.italic, format.underline,
                            format.alignLeft, format.listBullet, format.insertTable}) {
        QVERIFY(formatBar->actions().contains(action));
    }
    // Font family and size controls live on the formatting toolbar.
    QVERIFY(formatBar->findChild<QFontComboBox *>(QStringLiteral("fontFamilyCombo"))
            != nullptr);
    QVERIFY(formatBar->findChild<QComboBox *>(QStringLiteral("fontSizeCombo")) != nullptr);
}

void WriteUiTest::appMenusExist()
{
    WriteWindow window;
    QMenu *formatMenu = window.findChild<QMenu *>(QStringLiteral("formatMenu"));
    QMenu *insertMenu = window.findChild<QMenu *>(QStringLiteral("insertMenu"));
    QVERIFY2(formatMenu != nullptr, "Format menu must exist after finalizeAppMenuHooks");
    QVERIFY2(insertMenu != nullptr, "Insert menu must exist after finalizeAppMenuHooks");
    const WriteActions &format = window.formatController()->actions();
    QVERIFY(formatMenu->actions().contains(format.bold));
    QVERIFY(insertMenu->actions().contains(format.insertColumns));
    QVERIFY(insertMenu->actions().contains(format.insertTable));
}

void WriteUiTest::charFormatActionsApplyAndSync()
{
    WriteWindow window;
    const QTextCursor selection = selectFirstWord(&window);
    const WriteActions &actions = window.formatController()->actions();

    actions.bold->trigger();
    QTextCursor cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.charFormat().fontWeight(), int(QFont::Bold));
    QVERIFY(actions.bold->isChecked());

    actions.italic->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.charFormat().fontItalic());
    QVERIFY(actions.italic->isChecked());

    actions.underline->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.charFormat().fontUnderline());

    actions.strikeOut->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.charFormat().fontStrikeOut());

    // Toggling bold off leaves the others alone.
    actions.bold->trigger();
    cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.charFormat().fontWeight(), int(QFont::Normal));
    QVERIFY(cursor.charFormat().fontItalic());
    Q_UNUSED(selection);
}

void WriteUiTest::fontControlsApplyAndSync()
{
    WriteWindow window;
    selectFirstWord(&window);
    QFontComboBox *family =
        window.findChild<QFontComboBox *>(QStringLiteral("fontFamilyCombo"));
    QComboBox *size = window.findChild<QComboBox *>(QStringLiteral("fontSizeCombo"));
    QVERIFY(family != nullptr);
    QVERIFY(size != nullptr);

    // QFontComboBox resolves generic aliases; drive it with a family that
    // actually exists on this system and compare against what it emits.
    const QStringList installed = QFontDatabase::families();
    QVERIFY(!installed.isEmpty());
    family->setCurrentFont(QFont(installed.first()));
    const QString wanted = family->currentFont().family();
    QTextCursor cursor = window.pageView()->control()->textCursor();
    QVERIFY(!wanted.isEmpty());
    QVERIFY(cursor.charFormat().font().families().contains(wanted));

    size->setCurrentText(QStringLiteral("26"));
    emit size->activated(size->currentIndex());
    cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.charFormat().fontPointSize(), 26.0);
    // The control syncs the combo back through the controller.
    QCOMPARE(size->currentText(), QStringLiteral("26"));
}

void WriteUiTest::alignmentAndListsApply()
{
    WriteWindow window;
    selectFirstWord(&window);
    const WriteActions &actions = window.formatController()->actions();
    QTextCursor cursor = window.pageView()->control()->textCursor();

    actions.alignCenter->trigger();
    QCOMPARE(cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask,
             Qt::AlignHCenter);
    QVERIFY(actions.alignCenter->isChecked());

    actions.alignJustify->trigger();
    QCOMPARE(cursor.blockFormat().alignment() & Qt::AlignHorizontal_Mask,
             Qt::AlignJustify);
    QVERIFY(actions.alignJustify->isChecked());
    QVERIFY(!actions.alignCenter->isChecked());

    actions.listBullet->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.block().textList() != nullptr);
    QCOMPARE(cursor.block().textList()->format().style(), QTextListFormat::ListDisc);
    QVERIFY(actions.listBullet->isChecked());

    // Bullet -> numbered converts instead of nesting.
    actions.listNumbered->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.block().textList() != nullptr);
    QCOMPARE(cursor.block().textList()->format().style(), QTextListFormat::ListDecimal);
    QVERIFY(actions.listNumbered->isChecked());
    QVERIFY(!actions.listBullet->isChecked());

    // Toggling the active style removes the list.
    actions.listNumbered->trigger();
    cursor = window.pageView()->control()->textCursor();
    QVERIFY(cursor.block().textList() == nullptr);
    QVERIFY(!actions.listNumbered->isChecked());
}

void WriteUiTest::indentChangesBlockFormat()
{
    WriteWindow window;
    selectFirstWord(&window);
    const WriteActions &actions = window.formatController()->actions();
    actions.indentMore->trigger();
    QTextCursor cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.blockFormat().indent(), 1);
    actions.indentLess->trigger();
    cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.blockFormat().indent(), 0);
    // Never negative.
    actions.indentLess->trigger();
    cursor = window.pageView()->control()->textCursor();
    QCOMPARE(cursor.blockFormat().indent(), 0);
}

void WriteUiTest::tableAndColumnsInsert()
{
    WriteWindow window;
    selectFirstWord(&window);
    window.formatController()->insertTableAtCursor(2, 3);

    QTextDocument *doc = window.writeDocument()->textDocument();
    QTextTable *table = nullptr;
    const auto frames = doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        table = qobject_cast<QTextTable *>(frame);
        if (table != nullptr) {
            break;
        }
    }
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 3);
    QVERIFY(table->format().border() >= 1);

    // Columns: a borderless 1xN table (see the AGENT-NOTE in the
    // controller - QTextDocument has no native text-column sections).
    selectFirstWord(&window);
    window.formatController()->insertColumnsAtCursor(3, 24);
    QTextTable *columns = nullptr;
    for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
        if (auto *candidate = qobject_cast<QTextTable *>(frame);
            candidate != nullptr && candidate->rows() == 1 && candidate->columns() == 3) {
            columns = candidate;
        }
    }
    QVERIFY(columns != nullptr);
    QCOMPARE(columns->format().border(), 0.0);
    QCOMPARE(columns->format().cellPadding(), 12.0);
}

void WriteUiTest::findAndReplaceFlows()
{
    WriteWindow window;
    QTextDocument *doc = window.writeDocument()->textDocument();
    QTextCursor(doc).insertText(QStringLiteral("alpha beta gamma beta delta"));

    FindReplaceBar *bar = window.findReplaceBar();
    QVERIFY(bar->isHidden());
    window.action(QString::fromLatin1(::QindaOffice::Shell::ActionId::kEditFind))
        ->trigger();
    QVERIFY(!bar->isHidden());

    auto *findEdit = bar->findChild<QLineEdit *>(QStringLiteral("findEdit"));
    auto *replaceEdit = bar->findChild<QLineEdit *>(QStringLiteral("replaceEdit"));
    QVERIFY(findEdit != nullptr);
    QVERIFY(replaceEdit != nullptr);

    findEdit->setText(QStringLiteral("beta"));
    bar->findNext();
    QCOMPARE(window.pageView()->control()->textCursor().selectedText(),
             QStringLiteral("beta"));

    // Wrap-around: search again twice (second match, then wraps to first).
    bar->findNext();
    QCOMPARE(window.pageView()->control()->textCursor().selectedText(),
             QStringLiteral("beta"));
    bar->findNext();
    QCOMPARE(window.pageView()->control()->textCursor().selectedText(),
             QStringLiteral("beta"));

    replaceEdit->setText(QStringLiteral("BETA"));
    bar->replaceAll();
    const QString text = doc->toPlainText();
    QVERIFY(!text.contains(QStringLiteral(" beta ")));
    QCOMPARE(text.count(QStringLiteral("BETA")), 2);
}

void WriteUiTest::editActionsFollowControlAvailability()
{
    namespace ActionId = ::QindaOffice::Shell::ActionId;
    WriteWindow window;
    QAction *undo = window.action(QString::fromLatin1(ActionId::kEditUndo));
    QAction *copy = window.action(QString::fromLatin1(ActionId::kEditCopy));
    QVERIFY(!undo->isEnabled());
    QVERIFY(!copy->isEnabled());

    // A pristine document has no undo history (see the AGENT-NOTE in
    // write_document.cpp about the Qt setPageSize undo-item quirk).
    QCOMPARE(window.writeDocument()->textDocument()->availableUndoSteps(), 0);
    selectFirstWord(&window);
    QCoreApplication::processEvents();
    QVERIFY(undo->isEnabled());
    QVERIFY(copy->isEnabled());

    undo->trigger();
    QCOMPARE(window.writeDocument()->textDocument()->toPlainText(), QString());
}

void WriteUiTest::paperPaletteIsPinned()
{
    // ADR-0115 governs chrome, not paper: the page must render light with
    // dark default text under any desktop theme (the "white boxes on dark"
    // regression came from filling the page with the themed base color).
    WriteWindow window;
    const QPalette paper = window.pageView()->viewport()->palette();
    QCOMPARE(paper.color(QPalette::Base), QColor(Qt::white));
    QCOMPARE(paper.color(QPalette::Text), QColor(Qt::black));
}

void WriteUiTest::zoomActionsAdjustView()
{
    namespace ActionId = ::QindaOffice::Shell::ActionId;
    WriteWindow window;
    QAction *zoomIn = window.action(QString::fromLatin1(ActionId::kViewZoomIn));
    QAction *zoomReset = window.action(QString::fromLatin1(ActionId::kViewZoomReset));
    QVERIFY(zoomIn->isEnabled());
    const qreal before = window.pageView()->zoom();
    zoomIn->trigger();
    QVERIFY(window.pageView()->zoom() > before);
    zoomReset->trigger();
    QCOMPARE(window.pageView()->zoom(), 1.0);
}

void WriteUiTest::tableOperationsWork()
{
    WriteWindow window;
    selectFirstWord(&window);
    window.formatController()->insertTableAtCursor(2, 2);
    TableController *tables = window.tableController();
    QVERIFY(tables != nullptr);
    QMenu *tableMenu = window.findChild<QMenu *>(QStringLiteral("tableMenu"));
    QVERIFY(tableMenu != nullptr);
    QVERIFY(tables->actions().insertRowBelow->isEnabled());
    QVERIFY(tables->actions().mergeCells->isEnabled());

    QTextDocument *doc = window.writeDocument()->textDocument();
    QTextCursor cursor = window.pageView()->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 2);

    tables->actions().insertRowBelow->trigger();
    QCOMPARE(table->rows(), 3);
    tables->actions().insertColumnRight->trigger();
    QCOMPARE(table->columns(), 3);

    // Merge the cursor cell with its right neighbour, then split back.
    // Re-read the cursor after every mutation: positions shift as the
    // table geometry changes.
    tables->actions().mergeCells->trigger();
    QVERIFY(table->cellAt(window.pageView()->control()->textCursor()).columnSpan() >= 2);
    QVERIFY(tables->actions().splitCell->isEnabled());
    tables->actions().splitCell->trigger();
    QCOMPARE(table->cellAt(window.pageView()->control()->textCursor()).columnSpan(), 1);
    Q_UNUSED(cursor);

    tables->actions().deleteRow->trigger();
    QCOMPARE(table->rows(), 2);
    tables->actions().deleteColumn->trigger();
    QCOMPARE(table->columns(), 2);

    // Moving the cursor out of the table disables the ops.
    QTextCursor outside(doc);
    outside.movePosition(QTextCursor::End);
    window.pageView()->control()->setTextCursor(outside);
    QCoreApplication::processEvents();
    QVERIFY(!tables->actions().insertRowBelow->isEnabled());
}

void WriteUiTest::pageSetupRoundTripsThroughHtml()
{
    WriteWindow window;
    window.writeDocument()->textDocument()->setPlainText(QStringLiteral("body"));
    // Non-default page + margin + header/footer with tokens.
    window.writeDocument()->setupPaged(QSizeF(816, 1056), 64);
    window.writeDocument()->setHeaderFooter(QStringLiteral("Report & <Quotes>"),
                                            QStringLiteral("Page {page} of {pages}"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/doc.html");
    QString error;
    QVERIFY2(window.writeDocument()->save(path, &error), qPrintable(error));

    // The raw HTML carries the metas; the header is attribute-escaped.
    QFile raw(path);
    QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString html = QString::fromUtf8(raw.readAll());
    QVERIFY(html.contains(QStringLiteral("qindawrite-page")));
    QVERIFY(html.contains(QStringLiteral("qindawrite-header")));
    QVERIFY(html.contains(QStringLiteral("&amp;")));

    WriteDocument reloaded;
    QVERIFY2(reloaded.load(path, &error), qPrintable(error));
    QCOMPARE(reloaded.pageSize(), QSizeF(816, 1056));
    QCOMPARE(reloaded.pageMargin(), 64.0);
    QCOMPARE(reloaded.headerText(), QStringLiteral("Report & <Quotes>"));
    QCOMPARE(reloaded.footerText(), QStringLiteral("Page {page} of {pages}"));

    // Token expansion is a pure function shared by screen, print and PDF.
    QCOMPARE(formatPageDecoration(QStringLiteral("{page}/{pages}"), 2, 7),
             QStringLiteral("2/7"));
}

QTEST_MAIN(WriteUiTest)
#include "tst_write_ui.moc"
