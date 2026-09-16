// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"
#include "standard_actions.h"

#include "document.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QtTest>

using namespace QindaOffice::Shell;

namespace {

// The minimum an app engine is: a Document that is not PlainTextDocument,
// installed through setDocumentHost() the way all four apps install theirs.
class StubDocument final : public QindaOffice::Core::Document {
public:
    bool load(const QString &, QString * = nullptr) override { return true; }
    bool save(const QString &, QString * = nullptr) override { return true; }
    [[nodiscard]] QStringList mimeTypes() const override { return {}; }
    [[nodiscard]] QByteArray recoverySnapshot() const override { return {}; }
    bool loadRecovery(const QByteArray &, QString * = nullptr) override { return false; }
};

} // namespace

// Verifies the shared document-window frame: standard menu order
// (File/Edit/View/AI/Settings/Help), the View menu-bar action, and the
// fail-closed global-menu behaviour under the offscreen platform.
class DocumentWindowTest : public QObject {
    Q_OBJECT
private slots:
    void menuOrderFollowsShellSpec();
    void viewActionTogglesLocalMenuBar();
    void offscreenExportFailsClosedAndKeepsLocalBar();
    void actionLookupByObjectName();
    void documentHostEnablesSaveCommands();
    void closeAndQuitUseTheMandatedChords();
    void cleanupTestCase();
};

static QStringList menuTitles(DocumentWindow *window)
{
    QStringList titles;
    const QList<QAction *> actions = window->menuBar()->actions();
    for (QAction *action : actions) {
        titles.append(action->text());
    }
    return titles;
}

void DocumentWindowTest::menuOrderFollowsShellSpec()
{
    DocumentWindow window;
    const QStringList titles = menuTitles(&window);
    QCOMPARE(titles, QStringList({"&File", "&Edit", "&View", "&AI", "&Settings", "&Help"}));
}

void DocumentWindowTest::viewActionTogglesLocalMenuBar()
{
    DocumentWindow window;
    QAction *toggle = window.action(QString::fromLatin1(ActionId::kViewShowMenuBar));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isChecked());
    toggle->setChecked(false);
    QVERIFY(!window.menuBar()->isVisibleTo(&window));
    toggle->setChecked(true);
    QVERIFY(window.menuBar()->isVisibleTo(&window));
}

void DocumentWindowTest::offscreenExportFailsClosedAndKeepsLocalBar()
{
    // Under the offscreen platform there is no Wayland/xcb announcement, so
    // start() must fail closed: no export, local menu bar stays visible.
    DocumentWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.menuBar()->isVisibleTo(&window));
}

void DocumentWindowTest::actionLookupByObjectName()
{
    DocumentWindow window;
    QCOMPARE(window.action(QString::fromLatin1(ActionId::kFileNew)),
             window.standardActions()->fileNew);
    QVERIFY(window.action(QStringLiteral("noSuchAction")) == nullptr);
}

// AGENTS.md rule 1: every app installs its engine through setDocumentHost(),
// and a window that has a document can save. Save/Save As are created
// disabled ("no engine yet") and only setDocument()'s PlainTextDocument
// branch used to enable them, so File > Save, the toolbar Save button and
// Ctrl+S were dead in QindaWrite, QindaCalc, QindaShow and QindaNote alike.
void DocumentWindowTest::documentHostEnablesSaveCommands()
{
    DocumentWindow window;
    QVERIFY(!window.standardActions()->fileSave->isEnabled());
    QVERIFY(!window.standardActions()->fileSaveAs->isEnabled());
    window.setDocumentHost(new QLabel, new StubDocument);
    QVERIFY(window.standardActions()->fileSave->isEnabled());
    QVERIFY(window.standardActions()->fileSaveAs->isEnabled());
}

// Rule 1 names both chords. QKeySequence::Close/::Quit resolve through the
// platform theme's keyboard scheme, and the scheme the qindaqt theme yields
// binds them to Ctrl+F4 and the "Exit" media key instead.
void DocumentWindowTest::closeAndQuitUseTheMandatedChords()
{
    DocumentWindow window;
    QCOMPARE(window.standardActions()->fileCloseWindow->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+W")));
    QCOMPARE(window.standardActions()->fileQuit->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+Q")));
}

void DocumentWindowTest::cleanupTestCase()
{
    // Windows created in tests were stack-owned; nothing leaks by design.
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    DocumentWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_document_window.moc"
