// SPDX-License-Identifier: GPL-3.0-or-later
#include "standard_actions.h"

#include "action_model.h"
#include "menu_tree.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace QindaOffice::Quick {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("StandardActions", text);
}

} // namespace

void addStandardActions(ActionModel *model, const QString &appDisplayName)
{
    using namespace ActionId;
    const auto id = [](const char *raw) { return QString::fromLatin1(raw); };
    // AGENT-GUARD: Ctrl+W and Ctrl+Q are LITERAL, never QKeySequence::Close /
    // ::Quit — those resolve through the platform theme's keyboard scheme
    // to Ctrl+F4 and the "Exit" media key on the QindaQt desktop (D-011).
    model->add(id(kFileNew), tr("&New"), QKeySequence::New, QStringLiteral("file-plus"));
    model->add(id(kFileOpen), tr("&Open…"), QKeySequence::Open, QStringLiteral("folder-open"));
    model->add(id(kFileCloseWindow), tr("&Close Window"), QKeySequence(QStringLiteral("Ctrl+W")),
               QStringLiteral("x"));
    model->add(id(kFileSave), tr("&Save"), QKeySequence::Save, QStringLiteral("save"));
    model->add(id(kFileSaveAs), tr("Save &As…"), QKeySequence::SaveAs);
    model->add(id(kFilePrint), tr("&Print…"), QKeySequence::Print, QStringLiteral("printer"));
    // Print preview needs its OWN chord: sharing Save As's made both ambiguous.
    model->add(id(kFilePrintPreview), tr("Print Pre&view…"),
               QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    model->add(id(kFileQuit), tr("&Quit"), QKeySequence(QStringLiteral("Ctrl+Q")));

    model->add(id(kEditUndo), tr("&Undo"), QKeySequence::Undo, QStringLiteral("undo-2"));
    model->add(id(kEditRedo), tr("&Redo"), QKeySequence::Redo, QStringLiteral("redo-2"));
    model->add(id(kEditCut), tr("Cu&t"), QKeySequence::Cut, QStringLiteral("scissors"));
    model->add(id(kEditCopy), tr("&Copy"), QKeySequence::Copy, QStringLiteral("copy"));
    model->add(id(kEditPaste), tr("&Paste"), QKeySequence::Paste,
               QStringLiteral("clipboard-paste"));
    model->add(id(kEditSelectAll), tr("Select &All"), QKeySequence::SelectAll);
    model->add(id(kEditFind), tr("&Find…"), QKeySequence::Find, QStringLiteral("search"));
    model->add(id(kEditReplace), tr("Replace…"), QKeySequence::Replace);
    model->add(id(kEditFindNext), tr("Find &Next"), QKeySequence::FindNext);
    model->add(id(kEditFindPrevious), tr("Find &Previous"), QKeySequence::FindPrevious);

    model->add(id(kViewZoomIn), tr("Zoom &In"), QKeySequence::ZoomIn, QStringLiteral("zoom-in"));
    model->add(id(kViewZoomOut), tr("Zoom &Out"), QKeySequence::ZoomOut,
               QStringLiteral("zoom-out"));
    model->add(id(kViewZoomReset), tr("&Reset Zoom"), QKeySequence(QStringLiteral("Ctrl+0")));
    ActionEntry *menuBar = model->add(id(kViewShowMenuBar), tr("Show Menu &Bar"));
    menuBar->setCheckable(true);
    menuBar->setChecked(true);
    // AGENT-NOTE (D-041): the id predates the ribbon and stays (rule 5);
    // the toolbar band IS the ribbon now, so the label says so.
    ActionEntry *toolBars = model->add(id(kViewShowToolBars), tr("Show &Ribbon"));
    toolBars->setCheckable(true);
    toolBars->setChecked(true);
    ActionEntry *compact = model->add(id(kViewCompactRibbon), tr("&Compact Ribbon"), {},
                                      QStringLiteral("panel-top"));
    compact->setCheckable(true);
    ActionEntry *statusBar = model->add(id(kViewShowStatusBar), tr("Show &Status Bar"));
    statusBar->setCheckable(true);
    statusBar->setChecked(true);

    ActionEntry *assistant = model->add(id(kAiAssistant), tr("Assistant…"));
    assistant->setCheckable(true);

    ActionEntry *restore = model->add(id(kSettingsRestorePrevious),
                                      tr("Restore &Previous Documents"));
    restore->setCheckable(true);
    restore->setEnabled(false); // enabled by the application services
    model->add(id(kSettingsPreferences), tr("&Preferences…"));

    model->add(id(kHelpSearchCommands), tr("&Search Commands…"),
               QKeySequence(QStringLiteral("Alt+Q")), QStringLiteral("search"));
    model->add(id(kHelpAbout), tr("&About %1").arg(appDisplayName));
    model->add(id(kHelpAboutQt), tr("About &Qt"));

    // Honest disabled state until a host wires them (D-011/D-012).
    model->setEnabled(documentCommandIds(), false);
    model->setEnabled({id(kAiAssistant)}, false);
}

QStringList documentCommandIds()
{
    using namespace ActionId;
    QStringList ids;
    for (const char *raw : {kFileSave, kFileSaveAs, kFilePrint, kFilePrintPreview, kEditUndo,
                            kEditRedo, kEditCut, kEditCopy, kEditPaste, kEditSelectAll,
                            kEditFind, kEditReplace, kEditFindNext, kEditFindPrevious,
                            kViewZoomIn, kViewZoomOut, kViewZoomReset}) {
        ids.append(QString::fromLatin1(raw));
    }
    return ids;
}

QList<MenuNode> standardMenuTree()
{
    using namespace ActionId;
    const auto a = [](const char *raw) { return MenuNode::action(QString::fromLatin1(raw)); };
    QList<MenuNode> menus;
    menus.append(MenuNode::submenu(
        QStringLiteral("fileMenu"), tr("&File"),
        {a(kFileNew), a(kFileOpen),
         MenuNode::submenu(QStringLiteral("fileRecentMenu"), tr("Open &Recent")),
         MenuNode::separator(),
         // Apps insert their File entries BEFORE this one.
         a(kFileCloseWindow), MenuNode::separator(), a(kFileSave), a(kFileSaveAs),
         MenuNode::separator(), a(kFilePrint), a(kFilePrintPreview), MenuNode::separator(),
         a(kFileQuit)}));
    menus.append(MenuNode::submenu(
        QStringLiteral("editMenu"), tr("&Edit"),
        {a(kEditUndo), a(kEditRedo), MenuNode::separator(), a(kEditCut), a(kEditCopy),
         a(kEditPaste), MenuNode::separator(), a(kEditSelectAll), MenuNode::separator(),
         a(kEditFind), a(kEditReplace), a(kEditFindNext), a(kEditFindPrevious)}));
    menus.append(MenuNode::submenu(
        QStringLiteral("viewMenu"), tr("&View"),
        {a(kViewZoomIn), a(kViewZoomOut), a(kViewZoomReset), MenuNode::separator(),
         a(kViewShowToolBars), a(kViewCompactRibbon), a(kViewShowStatusBar), a(kViewShowMenuBar)}));
    // App menus slot in between View and AI (insertMenuBefore("aiMenu", …)).
    menus.append(MenuNode::submenu(QStringLiteral("aiMenu"), tr("&AI"), {a(kAiAssistant)}));
    menus.append(MenuNode::submenu(QStringLiteral("settingsMenu"), tr("&Settings"),
                                   {a(kSettingsRestorePrevious), MenuNode::separator(),
                                    a(kSettingsPreferences)}));
    menus.append(MenuNode::submenu(QStringLiteral("helpMenu"), tr("&Help"),
                                   {a(kHelpSearchCommands), MenuNode::separator(), a(kHelpAbout),
                                    a(kHelpAboutQt)}));
    return menus;
}

} // namespace QindaOffice::Quick
