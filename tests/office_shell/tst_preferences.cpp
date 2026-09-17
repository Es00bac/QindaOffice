// SPDX-License-Identifier: GPL-3.0-or-later
// D-014: the suite has a settings UI, and the window chrome it controls is
// really applied. Before this pass Settings ▸ Preferences… was created
// disabled and never enabled — a dead menu entry in all four apps — and no
// window created a status bar until something called showMessage(), which
// made the document area jump the first time anything was reported.

#include "document_window.h"
#include "preferences.h"
#include "preferences_dialog.h"
#include "standard_actions.h"

#include "document.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

using namespace QindaOffice::Shell;

class PreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void storeRoundTripsAndBoundsValues();
    void preferencesActionIsLive();
    void statusBarExistsFromConstruction();
    void chromeTogglesFollowTheStore();
    void dialogWritesThroughOnAccept();
    void fileMenuKeepsQuitLast();
};

void PreferencesTest::storeRoundTripsAndBoundsValues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/preferences.json");
    {
        Preferences prefs(path);
        QCOMPARE(prefs.defaultZoomPercent(), 100);
        QCOMPARE(prefs.autoSaveMinutes(), 0);
        QVERIFY(prefs.showStatusBar());
        prefs.setDefaultZoomPercent(10000); // out of range
        prefs.setAutoSaveMinutes(-5);
        prefs.setDocumentFontFamily(QStringLiteral("Courier"));
        prefs.setDocumentFontSize(13);
        prefs.setShowStatusBar(false);
    }
    // Values are written through on every change, so a crash never loses a
    // preference the user just set.
    Preferences reopened(path);
    QCOMPARE(reopened.defaultZoomPercent(), Preferences::kMaxZoomPercent);
    QCOMPARE(reopened.autoSaveMinutes(), 0);
    QCOMPARE(reopened.documentFontFamily(), QStringLiteral("Courier"));
    QCOMPARE(reopened.documentFontSize(), 13.0);
    QVERIFY(!reopened.showStatusBar());
}

void PreferencesTest::preferencesActionIsLive()
{
    DocumentWindow window;
    QAction *preferences = window.action(QString::fromLatin1(ActionId::kSettingsPreferences));
    QVERIFY(preferences != nullptr);
    // The one assertion that would have caught the dead entry.
    QVERIFY(preferences->isEnabled());
    QVERIFY(!preferences->objectName().isEmpty());
}

void PreferencesTest::statusBarExistsFromConstruction()
{
    DocumentWindow window;
    window.show();
    QVERIFY(window.findChild<QStatusBar *>(QStringLiteral("statusBar")) != nullptr);
    auto *info = window.findChild<QLabel *>(QStringLiteral("statusInfoLabel"));
    QVERIFY(info != nullptr);
    window.setStatusInfo(QStringLiteral("42 words"));
    QCOMPARE(window.statusInfo(), QStringLiteral("42 words"));
    // A transient message on the left must not displace the permanent field.
    window.statusBar()->showMessage(QStringLiteral("Saved"));
    QCOMPARE(window.statusInfo(), QStringLiteral("42 words"));
}

void PreferencesTest::chromeTogglesFollowTheStore()
{
    DocumentWindow window;
    window.addMainToolBar();
    window.show();
    QAction *toolbars = window.action(QString::fromLatin1(ActionId::kViewShowToolBars));
    QAction *statusBar = window.action(QString::fromLatin1(ActionId::kViewShowStatusBar));
    QVERIFY(toolbars != nullptr && statusBar != nullptr);
    QVERIFY(toolbars->isChecked() && statusBar->isChecked());

    toolbars->setChecked(false);
    statusBar->setChecked(false);
    QVERIFY(!window.statusBar()->isVisible());
    for (QToolBar *bar : window.findChildren<QToolBar *>()) {
        QVERIFY2(!bar->isVisible(), qPrintable(bar->objectName()));
    }
    // The toggles persist, so the next window of the app opens the same way.
    QVERIFY(!Preferences::instance().showToolBars());
    QVERIFY(!Preferences::instance().showStatusBar());

    toolbars->setChecked(true);
    statusBar->setChecked(true);
    QVERIFY(Preferences::instance().showToolBars());
    QVERIFY(Preferences::instance().showStatusBar());
}

void PreferencesTest::dialogWritesThroughOnAccept()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Preferences prefs(dir.path() + QStringLiteral("/preferences.json"));
    PreferencesDialog dialog(&prefs);
    auto *zoom = dialog.findChild<QSpinBox *>(QStringLiteral("prefDefaultZoom"));
    auto *autoSave = dialog.findChild<QSpinBox *>(QStringLiteral("prefAutoSave"));
    auto *statusBar = dialog.findChild<QCheckBox *>(QStringLiteral("prefShowStatusBar"));
    QVERIFY(zoom != nullptr && autoSave != nullptr && statusBar != nullptr);
    zoom->setValue(150);
    autoSave->setValue(5);
    statusBar->setChecked(false);

    // Cancel leaves the store untouched: the dialog applies on OK only.
    dialog.reject();
    QCOMPARE(prefs.defaultZoomPercent(), 100);
    QCOMPARE(prefs.autoSaveMinutes(), 0);

    auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("preferencesButtons"));
    QVERIFY(buttons != nullptr);
    buttons->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(prefs.defaultZoomPercent(), 150);
    QCOMPARE(prefs.autoSaveMinutes(), 5);
    QVERIFY(!prefs.showStatusBar());

    // An app page slots in after the shell's own.
    PreferencesDialog second(&prefs);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("appPage"));
    second.addPage(page, QStringLiteral("Writing"));
    QVERIFY(second.findChild<QWidget *>(QStringLiteral("appPage")) != nullptr);
}

void PreferencesTest::fileMenuKeepsQuitLast()
{
    DocumentWindow window;
    window.finalizeAppMenuHooks();
    auto *fileMenu = window.findChild<QMenu *>(QStringLiteral("fileMenu"));
    QVERIFY(fileMenu != nullptr);
    const QList<QAction *> actions = fileMenu->actions();
    QVERIFY(!actions.isEmpty());
    // AGENT-GUARD: app entries insert before fileMenuAnchor(); appending put
    // "New from Template" underneath Quit in three of the four apps.
    QCOMPARE(actions.last()->objectName(), QString::fromLatin1(ActionId::kFileQuit));
    QVERIFY(window.fileMenuAnchor() != nullptr);
    QVERIFY(actions.indexOf(window.fileMenuAnchor())
            < actions.indexOf(window.action(QString::fromLatin1(ActionId::kFileQuit))));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaOfficeTest"));
    PreferencesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_preferences.moc"
