// SPDX-License-Identifier: GPL-3.0-or-later
// The panel export of a QindaCalc host must pass the desktop's own
// validator, and EVERY exported command must resolve in the host's
// ActionModel (V5: the Widgets suite's panel was dead).

#include "calc_test_support.h"

#include "menu_source.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QUuid>

using namespace CalcTest;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

class CalcMenuExportTest final : public QObject {
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
        CalcHost host;
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
        // Review (M66, D-075): protection; links, outline and hide/unhide
        // joined Insert, Data and Format. Page Layout (M78, D-087).
        QCOMPARE(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                                      QStringLiteral("Insert"), QStringLiteral("Format"), QStringLiteral("Page Layout"),
                                      QStringLiteral("Data"),
                                      QStringLiteral("Sheet"), QStringLiteral("Review"), QStringLiteral("AI"),
                                      QStringLiteral("Settings"), QStringLiteral("Help")}));
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
        // The generated template ids are in the tree and stable.
        QVERIFY(ids.filter(QStringLiteral("calcTemplate_")).size() >= 1);
        // M90 commands keep the same stable identity on the panel and ribbon.
        for (const char *id : {"calcSolverAction", "calcInsertSparklineLineAction", "calcInsertSparklineColumnAction",
                               "calcInsertSparklineWinLossAction", "calcSparklineEditDataAction", "calcSparklineHighPointAction",
                               "calcSparklineColorsAction", "calcSparklineAxisDateAction", "calcSparklineGroupAction",
                               "calcSparklineUngroupAction", "calcSparklineClearGroupAction", "calcTableRenameAction",
                               "calcTableResizeAction", "calcPivotCalculatedFieldAction"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
        // Every M66 command is exported for the panel.
        for (const char *id : {"insertHyperlinkAction", "calcEditHyperlinkAction", "calcOpenHyperlinkAction",
                               "calcRemoveHyperlinkAction", "calcGroupAction", "calcUngroupAction",
                               "calcShowDetailAction", "calcHideDetailAction", "calcHideRowsAction",
                               "calcUnhideRowsAction", "calcHideColumnsAction", "calcUnhideColumnsAction",
                               "calcProtectSheetAction", "calcUnprotectSheetAction", "calcProtectWorkbookAction",
                               "calcUnprotectWorkbookAction", "calcLockCellAction", "calcHideFormulaAction"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
        // M74 (D-083): Insert ▸ PivotTable and every PivotTable command, the
        // styles included, reach the panel (Data ▸ PivotTable).
        for (const char *id : {"calcInsertPivotTableAction", "calcPivotRefreshAction", "calcPivotRefreshAllAction",
                               "calcPivotChangeSourceAction", "calcPivotFieldListAction", "calcPivotFieldSettingsAction",
                               "calcPivotValueFieldSettingsAction", "calcPivotOptionsAction", "calcPivotGroupAction",
                               "calcPivotUngroupAction", "calcPivotShowDetailsAction", "calcPivotLayoutTabularAction",
                               "calcPivotSubtotalsTopAction", "calcPivotGrandTotalsRowsAction", "calcPivotBlankRowsOnAction",
                               "calcPivotBandedRowsAction", "calcPivotMoveAction", "calcPivotClearAction",
                               "calcPivotStyle_PivotStyleLight16", "calcPivotStyle_PivotStyleMedium9", "calcPivotStyle_None"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
        // M78 (D-087): Page Layout, Page Break Preview, What-If Analysis, the
        // data tools and the conditional-format commands reach the panel.
        for (const char *id : {"calcPageSetupAction", "calcMarginsWideAction", "calcOrientationLandscapeAction",
                               "calcPaperSize_9", "calcPaperSizeMoreAction", "calcSetPrintAreaAction", "calcClearPrintAreaAction",
                               "calcInsertPageBreakAction", "calcRemovePageBreakAction", "calcResetPageBreaksAction",
                               "calcPrintTitlesAction", "calcHeaderFooterAction", "calcScaleFitSheetAction",
                               "calcPrintGridlinesAction", "calcPrintHeadingsAction", "calcViewPageBreakPreviewAction",
                               "calcFreezeTopRowAction", "calcFreezeFirstColumnAction", "calcGoalSeekAction",
                               "calcScenarioManagerAction", "calcDataTableAction", "calcTextToColumnsAction",
                               "calcFlashFillAction", "calcRemoveDuplicatesAction", "calcConsolidateAction",
                               "calcCircleInvalidAction", "calcClearCirclesAction", "calcCondFormatNewRuleAction",
                               "calcCondFormatDataBarsAction", "calcCondFormatIconSetAction", "calcCondFormatTop10Action",
                               "calcCondFormatClearSheetAction"}) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        }
    }
};

CALC_TEST_MAIN(CalcMenuExportTest)

#include "tst_calc_menu_export.moc"
