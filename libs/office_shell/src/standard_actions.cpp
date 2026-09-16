// SPDX-License-Identifier: GPL-3.0-or-later
#include "standard_actions.h"

#include <QCoreApplication>
#include <QIcon>
#include <QKeySequence>
#include <QObject>

namespace QindaOffice::Shell {

QList<QAction *> StandardActions::all() const
{
    return {fileNew,       fileOpen,       fileCloseWindow, fileSave,   fileSaveAs,
            filePrint,     filePrintPreview, fileQuit,
            editUndo,      editRedo,       editCut,         editCopy,   editPaste,
            editSelectAll, editFind,       editReplace,     editFindNext, editFindPrevious,
            viewZoomIn,    viewZoomOut,    viewZoomReset,   viewShowMenuBar,
            aiAssistant,   settingsPreferences, helpAbout,  helpAboutQt};
}

namespace {

// Freedesktop theme icons (ADR-0115: icons via QIcon::fromTheme only). A
// missing icon degrades to the action text, so every action still reads
// honestly on a minimal icon theme.
QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

StandardActions createStandardActions(QObject *parent)
{
    using namespace ActionId;
    StandardActions a;
    const QKeySequence ctrlN(QKeySequence::New);
    const QKeySequence ctrlO(QKeySequence::Open);
    const QKeySequence ctrlW(QKeySequence::Close);
    const QKeySequence ctrlS(QKeySequence::Save);
    const QKeySequence ctrlShiftS(QKeySequence::SaveAs);
    const QKeySequence ctrlShiftP(QStringLiteral("Ctrl+Shift+P")); // print preview
    const QKeySequence ctrlP(QKeySequence::Print);
    const QKeySequence ctrlQ(QKeySequence::Quit);
    const QKeySequence ctrlF(QKeySequence::Find);
    const QKeySequence ctrlH(QKeySequence::Replace);
    const QKeySequence f3(QKeySequence::FindNext);
    const QKeySequence shiftF3(QKeySequence::FindPrevious);
    const QKeySequence ctrlPlus(QKeySequence::ZoomIn);
    const QKeySequence ctrlMinus(QKeySequence::ZoomOut);
    const QKeySequence ctrl0(QStringLiteral("Ctrl+0"));

    a.fileNew = make(parent, kFileNew, QObject::tr("&New"), ctrlN, "document-new");
    a.fileOpen = make(parent, kFileOpen, QObject::tr("&Open…"), ctrlO, "document-open");
    a.fileCloseWindow = make(parent, kFileCloseWindow, QObject::tr("&Close Window"), ctrlW, "window-close");
    a.fileSave = make(parent, kFileSave, QObject::tr("&Save"), ctrlS, "document-save");
    a.fileSaveAs = make(parent, kFileSaveAs, QObject::tr("Save &As…"), ctrlShiftS, "document-save-as");
    a.filePrint = make(parent, kFilePrint, QObject::tr("&Print…"), ctrlP, "document-print");
    // AGENT-GUARD: print preview needs its OWN shortcut — sharing Save As's
    // Ctrl+Shift+S made the overload ambiguous and disabled both actions.
    a.filePrintPreview =
        make(parent, kFilePrintPreview, QObject::tr("Print Pre&view…"), ctrlShiftP);
    a.fileQuit = make(parent, kFileQuit, QObject::tr("&Quit"), ctrlQ, "application-exit");

    a.editUndo = make(parent, kEditUndo, QObject::tr("&Undo"), QKeySequence::Undo, "edit-undo");
    a.editRedo = make(parent, kEditRedo, QObject::tr("&Redo"), QKeySequence::Redo, "edit-redo");
    a.editCut = make(parent, kEditCut, QObject::tr("Cu&t"), QKeySequence::Cut, "edit-cut");
    a.editCopy = make(parent, kEditCopy, QObject::tr("&Copy"), QKeySequence::Copy, "edit-copy");
    a.editPaste = make(parent, kEditPaste, QObject::tr("&Paste"), QKeySequence::Paste, "edit-paste");
    a.editSelectAll =
        make(parent, kEditSelectAll, QObject::tr("Select &All"), QKeySequence::SelectAll,
             "edit-select-all");
    a.editFind = make(parent, kEditFind, QObject::tr("&Find…"), ctrlF, "edit-find");
    a.editReplace = make(parent, kEditReplace, QObject::tr("Replace…"), ctrlH, "edit-find-replace");
    a.editFindNext = make(parent, kEditFindNext, QObject::tr("Find &Next"), f3);
    a.editFindPrevious =
        make(parent, kEditFindPrevious, QObject::tr("Find &Previous"), shiftF3);

    a.viewZoomIn = make(parent, kViewZoomIn, QObject::tr("Zoom &In"), ctrlPlus, "zoom-in");
    a.viewZoomOut = make(parent, kViewZoomOut, QObject::tr("Zoom &Out"), ctrlMinus, "zoom-out");
    a.viewZoomReset = make(parent, kViewZoomReset, QObject::tr("&Reset Zoom"), ctrl0, "zoom-original");
    a.viewShowMenuBar = make(parent, kViewShowMenuBar, QObject::tr("Show Menu &Bar"));
    a.viewShowMenuBar->setCheckable(true);
    a.viewShowMenuBar->setChecked(true);

    a.aiAssistant = make(parent, kAiAssistant, QObject::tr("Assistant…"));

    a.settingsPreferences =
        make(parent, kSettingsPreferences, QObject::tr("&Preferences…"));

    a.helpAbout = make(parent, kHelpAbout, QObject::tr("&About %1").arg(qAppName()));
    a.helpAboutQt = make(parent, kHelpAboutQt, QObject::tr("About &Qt"));

    // M0 skeleton honesty: no document engine yet, so these stay disabled
    // until their owning milestones land (AGENT-NOTE: M1 save/open, M4/M5
    // print, M6 AI, M10 preferences wiring).
    const QList<QAction *> notYet = {a.fileSave,  a.fileSaveAs,      a.filePrint,
                                     a.filePrintPreview,
                                     a.editUndo,  a.editRedo,        a.editCut,
                                     a.editCopy,  a.editPaste,       a.editSelectAll,
                                     a.editFind,  a.editReplace,     a.editFindNext,
                                     a.editFindPrevious,
                                     a.viewZoomIn, a.viewZoomOut,    a.viewZoomReset,
                                     a.aiAssistant, a.settingsPreferences};
    for (QAction *action : notYet) {
        action->setEnabled(false);
    }
    return a;
}

} // namespace QindaOffice::Shell
