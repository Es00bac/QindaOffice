// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAction>

class QObject;

namespace QindaOffice::Shell {

// The suite-standard action objectNames (menu-export activation identity).
//
// AGENT-CONTRACT: these strings are how the panel's dbusmenu endpoint and any
// saved keybindings address a command; they must never be renamed or reused
// for a different command (mirror: text-editor.md action table). Add new
// ids for new commands.
namespace ActionId {
inline const char kFileNew[] = "fileNewAction";
inline const char kFileOpen[] = "fileOpenAction";
inline const char kFileCloseWindow[] = "fileCloseWindowAction";
inline const char kFileSave[] = "fileSaveAction";
inline const char kFileSaveAs[] = "fileSaveAsAction";
inline const char kFilePrint[] = "filePrintAction";
inline const char kFilePrintPreview[] = "filePrintPreviewAction";
inline const char kFileQuit[] = "fileQuitAction";
inline const char kEditUndo[] = "editUndoAction";
inline const char kEditRedo[] = "editRedoAction";
inline const char kEditCut[] = "editCutAction";
inline const char kEditCopy[] = "editCopyAction";
inline const char kEditPaste[] = "editPasteAction";
inline const char kEditSelectAll[] = "editSelectAllAction";
inline const char kEditFind[] = "editFindAction";
inline const char kEditReplace[] = "editReplaceAction";
inline const char kEditFindNext[] = "editFindNextAction";
inline const char kEditFindPrevious[] = "editFindPreviousAction";
inline const char kViewZoomIn[] = "viewZoomInAction";
inline const char kViewZoomOut[] = "viewZoomOutAction";
inline const char kViewZoomReset[] = "viewZoomResetAction";
inline const char kViewShowMenuBar[] = "viewShowMenuBarAction";
inline const char kAiAssistant[] = "aiAssistantAction";
inline const char kSettingsPreferences[] = "settingsPreferencesAction";
inline const char kHelpAbout[] = "helpAboutAction";
inline const char kHelpAboutQt[] = "helpAboutQtAction";

// Rich-text formatting commands (QindaWrite; shared with any text host).
inline const char kFormatBold[] = "formatBoldAction";
inline const char kFormatItalic[] = "formatItalicAction";
inline const char kFormatUnderline[] = "formatUnderlineAction";
inline const char kFormatStrikeOut[] = "formatStrikeOutAction";
inline const char kFormatTextColor[] = "formatTextColorAction";
inline const char kCalcFillColor[] = "calcFillColorAction";
inline const char kFormatClearFormat[] = "formatClearFormatAction";
inline const char kFormatAlignLeft[] = "formatAlignLeftAction";
inline const char kFormatAlignCenter[] = "formatAlignCenterAction";
inline const char kFormatAlignRight[] = "formatAlignRightAction";
inline const char kFormatAlignJustify[] = "formatAlignJustifyAction";
inline const char kFormatListBullet[] = "formatListBulletAction";
inline const char kFormatListNumbered[] = "formatListNumberedAction";
inline const char kFormatIndentMore[] = "formatIndentMoreAction";
inline const char kFormatIndentLess[] = "formatIndentLessAction";

// Document-building commands (QindaWrite inserts; app-specific inserts
// keep their own stable ids below).
inline const char kInsertTable[] = "insertTableAction";
inline const char kInsertImage[] = "insertImageAction";
inline const char kInsertColumns[] = "insertColumnsAction";
inline const char kInsertPageBreak[] = "insertPageBreakAction";

// QindaWrite table commands (M14).
inline const char kTableInsertRowAbove[] = "tableInsertRowAboveAction";
inline const char kTableInsertRowBelow[] = "tableInsertRowBelowAction";
inline const char kTableInsertColumnLeft[] = "tableInsertColumnLeftAction";
inline const char kTableInsertColumnRight[] = "tableInsertColumnRightAction";
inline const char kTableDeleteRow[] = "tableDeleteRowAction";
inline const char kTableDeleteColumn[] = "tableDeleteColumnAction";
inline const char kTableMergeCells[] = "tableMergeCellsAction";
inline const char kTableSplitCell[] = "tableSplitCellAction";

// QindaCalc commands.
inline const char kInsertChart[] = "insertChartAction";
inline const char kInsertSheet[] = "insertSheetAction";

// QindaShow commands.
inline const char kShowFillColor[] = "showFillColorAction";
inline const char kShowStrokeColor[] = "showStrokeColorAction";
inline const char kShowInsertTextBox[] = "showInsertTextBoxAction";
inline const char kShowInsertRectangle[] = "showInsertRectangleAction";
inline const char kShowInsertEllipse[] = "showInsertEllipseAction";
inline const char kShowInsertLine[] = "showInsertLineAction";
inline const char kShowInsertArrow[] = "showInsertArrowAction";
inline const char kShowInsertImage[] = "showInsertImageAction";
inline const char kSlideNew[] = "slideNewAction";
inline const char kSlideDuplicate[] = "slideDuplicateAction";
inline const char kSlideDelete[] = "slideDeleteAction";
inline const char kSlideStartShow[] = "slideStartShowAction";
} // namespace ActionId

// One bundle per document window so the action discipline (every command is
// a persistent window-scoped QAction with a stable objectName) lives in a
// single factory. DocumentWindow owns an instance; look actions up with
// DocumentWindow::action() or findChild<QAction*>(ActionId::...).
struct StandardActions {
    QAction *fileNew = nullptr;
    QAction *fileOpen = nullptr;
    QAction *fileCloseWindow = nullptr;
    QAction *fileSave = nullptr;
    QAction *fileSaveAs = nullptr;
    QAction *filePrint = nullptr;
    QAction *filePrintPreview = nullptr;
    QAction *fileQuit = nullptr;

    QAction *editUndo = nullptr;
    QAction *editRedo = nullptr;
    QAction *editCut = nullptr;
    QAction *editCopy = nullptr;
    QAction *editPaste = nullptr;
    QAction *editSelectAll = nullptr;
    QAction *editFind = nullptr;
    QAction *editReplace = nullptr;
    QAction *editFindNext = nullptr;
    QAction *editFindPrevious = nullptr;

    QAction *viewZoomIn = nullptr;
    QAction *viewZoomOut = nullptr;
    QAction *viewZoomReset = nullptr;
    QAction *viewShowMenuBar = nullptr;

    QAction *aiAssistant = nullptr;
    QAction *settingsPreferences = nullptr;
    QAction *helpAbout = nullptr;
    QAction *helpAboutQt = nullptr;

    [[nodiscard]] QList<QAction *> all() const;
};

// Builds the standard action set parented to `parent` (must be the owning
// window so shortcuts resolve window-scoped). Commands the M0 skeleton has
// no engine for yet (save/print/edit/zoom/AI/preferences) are created
// disabled rather than omitted, so menus show the stable structure honestly.
[[nodiscard]] StandardActions createStandardActions(QObject *parent);

} // namespace QindaOffice::Shell
