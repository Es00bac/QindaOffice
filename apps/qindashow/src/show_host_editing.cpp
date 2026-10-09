// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's editing of what PowerPoint decks hold (M67, D-076): the table
// commands (ShowTableActions) and the dialogs they ask for, Edit Data for
// charts (ChartDataEditor), table and chart edits as id-located undo
// commands, the notes pane under the slide (View ▸ Notes) and the
// presenter view on a single screen (Slide Show ▸ Presenter View).

#include "show_host.h"

#include "pptx_reader.h"
#include "presentation_controller.h"
#include "show_chart_data.h"
#include "show_commands.h"
#include "show_table_actions.h"

#include <QUndoStack>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {
// AGENT-CONTRACT: app-scoped command ids (AGENTS.md rule 5), named by the
// ribbon layout and the tests; never renamed, never reused.
const char kEditChartData[] = "showChartEditDataAction";
const char kViewNotes[] = "showViewNotesAction";
const char kPresenterView[] = "showPresenterViewAction";
} // namespace

void ShowHost::createEditing()
{
    m_tables = new ShowTableActions(this);
    connect(m_tables, &ShowTableActions::insertTableRequested, this, &ShowHost::insertTableRequested);
    connect(m_tables, &ShowTableActions::splitRequested, this, &ShowHost::splitCellsRequested);
    connect(m_tables, &ShowTableActions::shadingRequested, this, &ShowHost::tableShadingRequested);
    m_chartData = new ChartDataEditor(m_canvas, this);
    connect(m_canvas, &SlideCanvasItem::shapeEdited, this,
            [this](const QString &shapeId, const ShowModel::Shape &before, const ShowModel::Shape &after, const QString &label) {
                // M75: in the Slide Master view the view commits the edit.
                if (m_canvas->slide() != nullptr && !masterViewActive()) {
                    m_document->undoStack()->push(new ShapeEditCommand(m_document->presentation(), m_canvas->slide()->id,
                                                                       shapeId, before, after, label));
                }
            });
    // Typing into a table without any text takes the deck's font and colour.
    m_canvas->setTableTextLook([this] {
        ShowModel::TextRun run;
        if (const ShowModel::Slide *slide = m_canvas->slide()) {
            const ShowModel::NewTableStyle style = ShowModel::newTableStyle(*m_document->presentation(), *slide);
            run.fontFamily = style.fontFamily;
            run.color = style.textColor;
        }
        return run;
    });

    ActionEntry *editData = actions()->add(QString::fromLatin1(kEditChartData), tr("Edit Chart &Data…"), {},
                                           QStringLiteral("sheet"));
    connect(editData, &ActionEntry::triggered, this, [this] {
        if (m_chartData->load()) {
            emit chartDataRequested();
        }
    });
    const auto syncChart = [this, editData] {
        const ShowModel::Shape *shape = m_canvas->selectionCount() == 1 ? m_canvas->selectedShape() : nullptr;
        editData->setEnabled(shape != nullptr && shape->kind == ShowModel::ShapeKind::Chart && !shape->chart.isEmpty());
    };
    connect(m_canvas, &SlideCanvasItem::selectionChanged, this, syncChart);
    connect(m_canvas, &SlideCanvasItem::slideEdited, this, syncChart);
    syncChart();

    // The pane under the slide is on by default, as in PowerPoint's Normal view.
    ActionEntry *notes = actions()->add(QString::fromLatin1(kViewNotes), tr("&Notes"), {}, QStringLiteral("sticky-note"));
    notes->setCheckable(true);
    notes->setChecked(true);
    connect(actions()->add(QString::fromLatin1(kPresenterView), tr("&Presenter View"), QKeySequence(QStringLiteral("Alt+F5")),
                           QStringLiteral("presentation")),
            &ActionEntry::triggered, this, [this] {
                m_presenterView = true;
                startShow();
                if (m_player == nullptr) {
                    m_presenterView = false;
                }
            });
}

QList<MenuNode> ShowHost::chartMenuItems() const
{
    return {MenuNode::separator(), MenuNode::action(QString::fromLatin1(kEditChartData))};
}

void ShowHost::insertTable(int rows, int columns)
{
    m_tables->insertTable(rows, columns);
}

void ShowHost::splitCells(int columns, int rows)
{
    m_tables->split(columns, rows);
}

void ShowHost::applyTableShading(const QColor &color)
{
    m_tables->applyShading(color);
}

void ShowHost::setTablePenColor(const QColor &color)
{
    if (color.isValid()) {
        m_tables->setPenColor(color);
    }
}

bool ShowHost::applyChartData()
{
    return m_chartData->apply();
}

} // namespace QindaOffice::Apps::Show
