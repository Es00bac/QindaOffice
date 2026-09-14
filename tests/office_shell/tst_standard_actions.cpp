// SPDX-License-Identifier: GPL-3.0-or-later
#include "standard_actions.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QtTest>

using namespace QindaOffice::Shell;

// Verifies the action discipline: one persistent QAction per standard
// command, stable objectName (the dbusmenu activation identity), standard
// shortcuts, and honest disabled state for engine-less commands in the M0
// skeleton.
class StandardActionsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void allActionsPresent();
    void objectNamesAreStable();
    void standardShortcuts();
    void enginelessCommandsStartDisabled();
};

void StandardActionsTest::initTestCase()
{
    // The app identity must be pinned before any window maps (shell spec).
    QCOMPARE(QApplication::desktopFileName(), QStringLiteral("org.qindaqt.QindaWrite"));
}

void StandardActionsTest::allActionsPresent()
{
    StandardActions actions = createStandardActions(this);
    for (QAction *action : actions.all()) {
        QVERIFY2(action != nullptr, "every slot in StandardActions is filled");
        QVERIFY(!action->objectName().isEmpty());
    }
}

void StandardActionsTest::objectNamesAreStable()
{
    StandardActions a = createStandardActions(this);
    QCOMPARE(a.fileNew->objectName(), QString::fromLatin1(ActionId::kFileNew));
    QCOMPARE(a.fileOpen->objectName(), QString::fromLatin1(ActionId::kFileOpen));
    QCOMPARE(a.fileCloseWindow->objectName(), QString::fromLatin1(ActionId::kFileCloseWindow));
    QCOMPARE(a.fileSave->objectName(), QString::fromLatin1(ActionId::kFileSave));
    QCOMPARE(a.fileSaveAs->objectName(), QString::fromLatin1(ActionId::kFileSaveAs));
    QCOMPARE(a.filePrint->objectName(), QString::fromLatin1(ActionId::kFilePrint));
    QCOMPARE(a.filePrintPreview->objectName(), QString::fromLatin1(ActionId::kFilePrintPreview));
    QCOMPARE(a.fileQuit->objectName(), QString::fromLatin1(ActionId::kFileQuit));
    QCOMPARE(a.editUndo->objectName(), QString::fromLatin1(ActionId::kEditUndo));
    QCOMPARE(a.editRedo->objectName(), QString::fromLatin1(ActionId::kEditRedo));
    QCOMPARE(a.editCut->objectName(), QString::fromLatin1(ActionId::kEditCut));
    QCOMPARE(a.editCopy->objectName(), QString::fromLatin1(ActionId::kEditCopy));
    QCOMPARE(a.editPaste->objectName(), QString::fromLatin1(ActionId::kEditPaste));
    QCOMPARE(a.editSelectAll->objectName(), QString::fromLatin1(ActionId::kEditSelectAll));
    QCOMPARE(a.editFind->objectName(), QString::fromLatin1(ActionId::kEditFind));
    QCOMPARE(a.editReplace->objectName(), QString::fromLatin1(ActionId::kEditReplace));
    QCOMPARE(a.editFindNext->objectName(), QString::fromLatin1(ActionId::kEditFindNext));
    QCOMPARE(a.editFindPrevious->objectName(), QString::fromLatin1(ActionId::kEditFindPrevious));
    QCOMPARE(a.viewZoomIn->objectName(), QString::fromLatin1(ActionId::kViewZoomIn));
    QCOMPARE(a.viewZoomOut->objectName(), QString::fromLatin1(ActionId::kViewZoomOut));
    QCOMPARE(a.viewZoomReset->objectName(), QString::fromLatin1(ActionId::kViewZoomReset));
    QCOMPARE(a.viewShowMenuBar->objectName(), QString::fromLatin1(ActionId::kViewShowMenuBar));
    QCOMPARE(a.aiAssistant->objectName(), QString::fromLatin1(ActionId::kAiAssistant));
    QCOMPARE(a.settingsPreferences->objectName(), QString::fromLatin1(ActionId::kSettingsPreferences));
    QCOMPARE(a.helpAbout->objectName(), QString::fromLatin1(ActionId::kHelpAbout));
    QCOMPARE(a.helpAboutQt->objectName(), QString::fromLatin1(ActionId::kHelpAboutQt));
}

void StandardActionsTest::standardShortcuts()
{
    StandardActions a = createStandardActions(this);
    QCOMPARE(a.fileNew->shortcut(), QKeySequence(QKeySequence::New));
    QCOMPARE(a.fileOpen->shortcut(), QKeySequence(QKeySequence::Open));
    QCOMPARE(a.fileCloseWindow->shortcut(), QKeySequence(QKeySequence::Close));
    QCOMPARE(a.fileSave->shortcut(), QKeySequence(QKeySequence::Save));
    QCOMPARE(a.fileSaveAs->shortcut(), QKeySequence(QKeySequence::SaveAs));
    QCOMPARE(a.filePrint->shortcut(), QKeySequence(QKeySequence::Print));
    QCOMPARE(a.fileQuit->shortcut(), QKeySequence(QKeySequence::Quit));
    QCOMPARE(a.editFind->shortcut(), QKeySequence(QKeySequence::Find));
    QCOMPARE(a.editReplace->shortcut(), QKeySequence(QKeySequence::Replace));
    QCOMPARE(a.editFindNext->shortcut(), QKeySequence(QKeySequence::FindNext));
    QCOMPARE(a.editFindPrevious->shortcut(), QKeySequence(QKeySequence::FindPrevious));
    QCOMPARE(a.viewZoomIn->shortcut(), QKeySequence(QKeySequence::ZoomIn));
    QCOMPARE(a.viewZoomOut->shortcut(), QKeySequence(QKeySequence::ZoomOut));
    QCOMPARE(a.viewZoomReset->shortcut(), QKeySequence(QStringLiteral("Ctrl+0")));
}

void StandardActionsTest::enginelessCommandsStartDisabled()
{
    StandardActions a = createStandardActions(this);
    const QList<QAction *> disabled = {a.fileSave,     a.fileSaveAs, a.filePrint,
                                       a.filePrintPreview,
                                       a.editUndo,     a.editRedo,   a.editCut,
                                       a.editCopy,     a.editPaste,  a.editSelectAll,
                                       a.editFind,     a.editReplace, a.editFindNext,
                                       a.editFindPrevious,
                                       a.viewZoomIn,   a.viewZoomOut, a.viewZoomReset,
                                       a.aiAssistant,  a.settingsPreferences};
    for (QAction *action : disabled) {
        QVERIFY2(!action->isEnabled(), qPrintable(action->objectName()));
    }
    QVERIFY(a.viewShowMenuBar->isCheckable());
    QVERIFY(a.viewShowMenuBar->isChecked());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    StandardActionsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_standard_actions.moc"
