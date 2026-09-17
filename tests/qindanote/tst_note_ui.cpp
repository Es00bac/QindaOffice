// SPDX-License-Identifier: GPL-3.0-or-later
// M22 chrome gates (plan "QindaNote UX overhaul" sections 3.2/3.8): the
// tool strip is icon-only where the suite ships glyphs (defect C1: a
// column of text buttons, each as wide as its label) and still shows
// EVERY command when squeezed to a third of the window width.

#include "qindanote/src/note_window.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "wrapping_tool_bar.h"

using namespace QindaOffice::Apps::Notes;

class NoteUiTest : public QObject {
    Q_OBJECT
private slots:
    void stripIsIconOnlyAndWrapsAtAThirdWidth();
};

void NoteUiTest::stripIsIconOnlyAndWrapsAtAThirdWidth()
{
    NoteWindow window;
    window.resize(1400, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *bar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(
        QStringLiteral("noteToolsToolBar"));
    QVERIFY(bar != nullptr);
    QVERIFY2(bar->stripActions().size() >= 7,
             qPrintable(QStringLiteral("only %1 commands on the strip")
                            .arg(bar->stripActions().size())));
    for (QAction *action : bar->stripActions()) {
        QVERIFY2(!action->objectName().isEmpty(), "every strip command is addressable");
    }

    // The tools and block commands have shipped glyphs (draw-*, insert-*,
    // checkbox): those buttons are icon-ONLY. A null icon falls back to
    // text on purpose (WrappingToolBar's contract), so an icon that stops
    // resolving must fail this test rather than silently re-widen the
    // strip.
    const QStringList iconOnly = {
        QStringLiteral("notePenTool"), QStringLiteral("noteHighlighterTool"),
        QStringLiteral("noteEraserTool"), QStringLiteral("noteTextTool"),
        QStringLiteral("noteAddTextAction"), QStringLiteral("noteAddCheckAction"),
        QStringLiteral("noteAddImageAction"),
    };
    const QList<QToolButton *> buttons = bar->findChildren<QToolButton *>();
    for (const QString &id : iconOnly) {
        QAction *action = nullptr;
        for (QAction *candidate : bar->stripActions()) {
            if (candidate->objectName() == id) {
                action = candidate;
            }
        }
        QVERIFY2(action != nullptr, qPrintable(id));
        QToolButton *button = nullptr;
        for (QToolButton *candidate : buttons) {
            if (candidate->defaultAction() == action) {
                button = candidate;
            }
        }
        QVERIFY2(button != nullptr, qPrintable(id));
        QVERIFY2(!button->icon().isNull(), qPrintable(id));
        QVERIFY2(button->toolButtonStyle() == Qt::ToolButtonIconOnly, qPrintable(id));
    }

    // Squeeze to a third of the width: the strip grows rows, and every
    // command keeps a visible button inside the strip. The assertion reads
    // the STRIP's height, not the toolbar's: offscreen the shell main
    // toolbar's theme icons do not resolve, so it balloons with text
    // buttons and shares its row height with every toolbar in the area —
    // bar->height() measures that neighbour, not the wrap.
    const int wideHeight = bar->strip()->height();
    window.resize(520, 700);
    QTRY_VERIFY(bar->strip()->height() > wideHeight);
    const QList<QToolButton *> squeezed = bar->findChildren<QToolButton *>();
    for (QAction *action : bar->stripActions()) {
        bool shown = false;
        for (QToolButton *button : squeezed) {
            if (button->defaultAction() == action && button->isVisible()
                && button->geometry().right() <= bar->strip()->width()) {
                shown = true;
                break;
            }
        }
        QVERIFY2(shown, qPrintable(action->objectName()));
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));
    // Resolve the suite's shipped SVGs from the source tree: offscreen
    // there is no desktop icon theme installed, and a null icon legitimately
    // falls back to text — which is exactly what this test must not see.
    // Qt only treats a directory with an index.theme as a theme, and the
    // suite deliberately never ships hicolor/index.theme (D-013), so the
    // test composes a throwaway theme around the real SVGs.
    QTemporaryDir themeRoot;
    const QString themeDir = themeRoot.path() + QStringLiteral("/qindatest");
    const bool prepared = themeRoot.isValid()
        && QDir().mkpath(themeDir + QStringLiteral("/scalable/actions"));
    if (!prepared) {
        qFatal("cannot build the throwaway icon theme");
    }
    QFile index(themeDir + QStringLiteral("/index.theme"));
    if (!index.open(QIODevice::WriteOnly)) {
        qFatal("cannot write the throwaway index.theme");
    }
    index.write("[Icon Theme]\nName=QindaTest\nDirectories=scalable/actions\n\n"
                "[scalable/actions]\nSize=24\nType=Scalable\nMinSize=8\nMaxSize=128\n");
    index.close();
    const QDir svgs(QStringLiteral(QINDA_ICONS_ROOT)
                    + QStringLiteral("/hicolor/scalable/actions"));
    for (const QString &file : svgs.entryList(QStringList{QStringLiteral("*.svg")})) {
        if (!QFile::copy(svgs.absoluteFilePath(file),
                         themeDir + QStringLiteral("/scalable/actions/") + file)) {
            qFatal("cannot copy %s", qPrintable(file));
        }
    }
    QIcon::setThemeSearchPaths({themeRoot.path()});
    QIcon::setThemeName(QStringLiteral("qindatest"));
    NoteUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_note_ui.moc"
