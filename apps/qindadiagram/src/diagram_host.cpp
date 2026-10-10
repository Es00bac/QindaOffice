// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramHost: the drawing document, the view toggles and printing.
#include "diagram_host.h"

#include "drawing_document.h"
#include "export.h"
#include "standard_actions.h"

#include <QPrinter>
#include <QUndoStack>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;
namespace ActionId = Quick::ActionId;

namespace {

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

} // namespace

DiagramHost::DiagramHost()
    : m_document(new DrawingDocument)
{
    setDocument(m_document);
    m_service = new DiagramService(m_document, this);
    createActions();
    createM87Actions();
    wireActions();
    wireM87Actions();
    insertMenus();

    // Undo/redo follow the drawing's stack.
    QUndoStack *stack = m_document->undoStack();
    Quick::ActionEntry *undo = action(id(ActionId::kEditUndo));
    Quick::ActionEntry *redo = action(id(ActionId::kEditRedo));
    connect(stack, &QUndoStack::canUndoChanged, undo, &Quick::ActionEntry::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, redo, &Quick::ActionEntry::setEnabled);
    connect(undo, &Quick::ActionEntry::triggered, stack, &QUndoStack::undo);
    connect(redo, &Quick::ActionEntry::triggered, stack, &QUndoStack::redo);
    undo->setEnabled(false);
    redo->setEnabled(false);

    const auto sync = [this] {
        syncEnablement();
        syncChecks();
        const QString kind = m_service->selectionKind();
        QStringList contexts;
        if (kind == QLatin1String("shape") || kind == QLatin1String("mixed")) {
            contexts << QStringLiteral("shape");
        }
        if (kind == QLatin1String("connector") || kind == QLatin1String("mixed")) {
            contexts << QStringLiteral("connector");
        }
        // M87: Visio's Container tab while a container or a lane is selected.
        if (!m_service->containerInfo().isEmpty()) {
            contexts << QStringLiteral("container");
        }
        syncM87();
        setRibbonContexts(contexts);
    };
    connect(m_service, &DiagramService::selectionChanged, this, sync);
    connect(m_service, &DiagramService::changed, this, sync);
    connect(m_service, &DiagramService::currentPageChanged, this, sync);
    connect(m_service, &DiagramService::toolChanged, this, sync);
    connect(m_service, &DiagramService::formatPainterChanged, this, sync);
    connect(m_service, &DiagramService::clipboardChanged, this, sync);
    finalizeHost();
    sync();
}

DiagramHost::~DiagramHost() = default;

QString DiagramHost::filterString() const
{
    return tr("Drawings (*.qdiagram *.vsdx *.vsdm *.vstx *.vstm);;QindaDiagram drawing (*.qdiagram);;"
              "Visio drawing (*.vsdx *.vsdm *.vstx *.vstm);;Visio 2003–2010 drawing (*.vsd *.vdx);;All files (*)");
}

QString DiagramHost::saveFilterString() const
{
    // AGENT-NOTE: the suffix decides the format (DrawingDocument::save), and
    // the first pattern of the chosen filter is what a bare name gets.
    return tr("QindaDiagram drawing (*.qdiagram);;Visio drawing (*.vsdx);;Visio template (*.vstx)");
}

void DiagramHost::documentReplaced()
{
    syncEnablement();
    syncChecks();
    // What a Visio file holds that is shown approximately or only kept.
    const QStringList warnings = m_document->loadWarnings();
    if (!warnings.isEmpty()) {
        showStatusMessage(warnings.join(QLatin1Char(' ')), 12000);
    }
}

Quick::PrintRenderCallback DiagramHost::printRenderCallback() const
{
    // Built when the job is created, so it prints the drawing as it is now.
    // AGENT-CONTRACT (plan §5.2): Print, Print Preview and Export PDF all
    // render this one painter; the canvas draws with the same engine code.
    PagesPainter painter = pagesPainter(m_document->drawing());
    return [painter = std::move(painter)](QPrinter *printer, QPainter *qp) { painter(printer, qp); };
}

bool DiagramHost::exportPdfTo(const QUrl &file)
{
    Quick::PrintJob *job = createPrintJob();
    if (job == nullptr) {
        return false;
    }
    const Drawing &drawing = m_document->drawing();
    job->setLandscape(!drawing.pages.isEmpty() && drawing.pages.first().size.width() > drawing.pages.first().size.height());
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const bool ok = job->exportPdf(path);
    showStatusMessage(ok ? tr("Exported %1").arg(path) : tr("Export failed: %1").arg(job->lastError()), ok ? 5000 : 8000);
    return ok;
}

void DiagramHost::setViewCenter(QPointF centre)
{
    if (centre == m_viewCenter) {
        return;
    }
    m_viewCenter = centre;
    emit viewCenterChanged();
}

bool DiagramHost::toggled(const char *raw) const
{
    const Quick::ActionEntry *entry = action(QString::fromLatin1(raw));
    return entry != nullptr && entry->isChecked();
}

bool DiagramHost::shapesPanelVisible() const
{
    return toggled("diagramShowShapesPanelAction");
}

void DiagramHost::setShapesPanelVisible(bool visible)
{
    action(id("diagramShowShapesPanelAction"))->setChecked(visible);
}

bool DiagramHost::inspectorVisible() const
{
    return toggled("diagramShowInspectorAction");
}

void DiagramHost::setInspectorVisible(bool visible)
{
    action(id("diagramShowInspectorAction"))->setChecked(visible);
}

bool DiagramHost::minimapVisible() const
{
    return toggled("diagramShowMinimapAction");
}

void DiagramHost::setMinimapVisible(bool visible)
{
    action(id("diagramShowMinimapAction"))->setChecked(visible);
}

bool DiagramHost::rulersVisible() const
{
    return toggled("diagramShowRulersAction");
}

void DiagramHost::setRulersVisible(bool visible)
{
    action(id("diagramShowRulersAction"))->setChecked(visible);
}

bool DiagramHost::snapEnabled() const
{
    return toggled("diagramSnapAction");
}

void DiagramHost::setSnapEnabled(bool enabled)
{
    action(id("diagramSnapAction"))->setChecked(enabled);
}

} // namespace QindaOffice::Apps::Diagram
