// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaShow host must pass the desktop's own
// validator, and EVERY exported command must resolve in the host's
// ActionModel (V5: the Widgets suite's panel was dead). Exactly one AI
// menu, with the inline actions and deck generation after Assistant….
// M75 (D-084): the Design and Review menus, Section, Video / Audio,
// Playback and the Slide Master view's commands are exported too.

#include "show_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QUuid>

using namespace ShowTest;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class ShowMenuExportTest final : public QObject {
    Q_OBJECT

    static void collectActions(const Protocol::MenuItem &item, QStringList &ids)
    {
        if (item.kind == Protocol::MenuItemKind::Action) {
            ids.append(item.id);
        }
        for (const Protocol::MenuItem &child : item.children) {
            collectActions(child, ids);
        }
    }

private slots:
    void exportedTreeIsCompleteValidAndResolves()
    {
        ShowHost host;
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY2(snapshot.complete, qPrintable(QStringLiteral("incomplete: ") + snapshot.defectCode));
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY2(result.accepted,
                 qPrintable(QStringLiteral("rejected: %1 at %2").arg(result.reasonCode, result.path)));
        QStringList titles;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            titles.append(item.text);
        }
        QCOMPARE(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                                      QStringLiteral("Slide"), QStringLiteral("Slide Show"), QStringLiteral("Insert"), QStringLiteral("Shape"),
                                      QStringLiteral("Arrange"), QStringLiteral("Table"), QStringLiteral("Design"),
                                      QStringLiteral("Review"), QStringLiteral("AI"), QStringLiteral("Settings"),
                                      QStringLiteral("Help")}));
        QCOMPARE(titles.count(QStringLiteral("AI")), 1);
        QStringList ids;
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collectActions(item, ids);
        }
        QVERIFY2(ids.size() > 60, qPrintable(QString::number(ids.size())));
        QStringList unresolved;
        for (const QString &id : ids) {
            if (host.actions()->entry(id) == nullptr) {
                unresolved.append(id);
            }
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join(QLatin1Char(' '))));
        QVERIFY(ids.filter(QStringLiteral("showAi")).size() >= 3);
        QVERIFY(ids.contains(QStringLiteral("showGenerateDeckAction")));
        QVERIFY(ids.contains(QStringLiteral("showSnapToGuidesAction"))); // appended to View
        QVERIFY(ids.filter(QStringLiteral("showAnim")).size() >= 7);
        QVERIFY(ids.filter(QStringLiteral("showTemplate_")).size() >= 1);
        // M75: every built-in theme, colour, font and effect set, and the
        // view, section, comment and media commands.
        QVERIFY(ids.filter(QStringLiteral("showTheme_")).size() >= 7);
        QVERIFY(ids.filter(QStringLiteral("showThemeColors_")).size() >= 8);
        QVERIFY(ids.filter(QStringLiteral("showThemeFonts_")).size() >= 8);
        QVERIFY(ids.filter(QStringLiteral("showThemeEffects_")).size() >= 3);
        for (const char *id : {"showSlideSizeWidescreenAction", "showSlideSizeStandardAction", "showSlideSizeCustomAction",
                               "showFormatBackgroundAction", "showThemeBrowseAction", "showSlideMasterViewAction",
                               "showInsertLayoutAction", "showMasterTextStylesAction", "showAddSectionAction",
                               "showCollapseSectionsAction", "showNewCommentAction", "showCommentsPaneAction",
                               "showInsertVideoAction", "showInsertAudioAction", "showMediaPlayAction", "showMediaLoopAction"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
    }
};

SHOW_TEST_MAIN(ShowMenuExportTest)

#include "tst_show_menu_export.moc"
