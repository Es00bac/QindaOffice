// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the document, the view, and the print source.
#include "plan_host.h"

#include "gantt_print.h"
#include "network_print.h"
#include "plan_document.h"
#include "plan_page_output.h"
#include "plan_reports.h"
#include "plan_time.h"
#include "report_print.h"

#include <QPrinter>
#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

PlanHost::PlanHost()
    : m_document(new PlanDocument)
{
    setDocument(m_document);
    m_service = new PlanService(m_document, this);
    createActions();
    insertMenus();
    connect(m_service, &PlanService::changed, this, [this] {
        syncEnablement();
        syncChecks();
    });
    connect(m_service, &PlanService::selectionChanged, this, [this] {
        syncEnablement();
        syncChecks();
    });
    connect(m_service, &PlanService::viewChanged, this, &PlanHost::syncChecks);
    connect(m_service, &PlanService::notify, this, [this](const QString &title, const QString &text, const QString &variant) {
        Q_UNUSED(title)
        Q_UNUSED(variant)
        showStatusMessage(text, 8000);
    });
    setRibbonContexts({QStringLiteral("ganttFormat")});
    finalizeHost();
    syncEnablement();
    syncChecks();
}

PlanHost::~PlanHost() = default;

QString PlanHost::filterString() const
{
    return tr("QindaPlan project (*.qplan);;Microsoft Project XML (*.xml);;All files (*)");
}

QString PlanHost::saveFilterString() const
{
    return tr("QindaPlan project (*.qplan);;Microsoft Project XML (*.xml)");
}

void PlanHost::documentReplaced()
{
    const QStringList warnings = m_document->loadWarnings();
    if (!warnings.isEmpty()) {
        emit messageRequested(tr("Opened with notes"), warnings.join(QLatin1Char('\n')), QStringLiteral("warning"));
    }
    syncEnablement();
}

QStringList PlanHost::viewKeys()
{
    // AGENT-GUARD: the first six keep their order (Ctrl+1…6, createActions).
    return {QStringLiteral("gantt"),    QStringLiteral("sheet"),    QStringLiteral("resources"), QStringLiteral("usage"),
            QStringLiteral("calendar"), QStringLiteral("reports"),  QStringLiteral("tracking"),  QStringLiteral("leveling"),
            QStringLiteral("network"),  QStringLiteral("taskUsage")};
}

void PlanHost::setCurrentView(const QString &view)
{
    if (!viewKeys().contains(view)) {
        return;
    }
    if (m_view != view) {
        const QString previous = m_view;
        m_view = view;
        // M79: a Gantt view draws in its own style; each sheet view keeps
        // its own table (switchTable, plan_host_views.cpp).
        m_service->setGanttStyle(view == QLatin1String("tracking")   ? QStringLiteral("tracking")
                                 : view == QLatin1String("leveling") ? QStringLiteral("leveling")
                                                                     : QStringLiteral("gantt"));
        switchTable(previous, view);
        emit currentViewChanged();
        setRibbonContexts(isGanttView() ? QStringList{QStringLiteral("ganttFormat")} : QStringList{});
        syncEnablement();
    }
    if (Quick::ActionEntry *entry = action(viewActionId(view))) {
        entry->setChecked(true);
    }
}

void PlanHost::setReportId(const QString &id)
{
    if (id != m_reportId && ::QindaOffice::Plan::reportIds().contains(id)) {
        m_reportId = id;
        emit reportIdChanged();
    }
}

void PlanHost::setZoom(double zoom)
{
    zoom = std::clamp(zoom, 0.25, 4.0);
    if (!qFuzzyCompare(zoom, m_zoom)) {
        m_zoom = zoom;
        emit zoomChanged();
    }
}

void PlanHost::setFindText(const QString &text)
{
    if (text != m_findText) {
        m_findText = text;
        emit findTextChanged();
        syncEnablement();
    }
}

bool PlanHost::showTimeline() const
{
    const Quick::ActionEntry *entry = action(QStringLiteral("planShowTimelineAction"));
    return entry != nullptr && entry->isChecked();
}

void PlanHost::setPrintFrom(const QString &iso)
{
    if (iso != m_printFrom) {
        m_printFrom = iso;
        emit printSetupChanged();
    }
}

void PlanHost::setPrintTo(const QString &iso)
{
    if (iso != m_printTo) {
        m_printTo = iso;
        emit printSetupChanged();
    }
}

void PlanHost::setPrintPagesWide(int pages)
{
    pages = std::clamp(pages, 1, 20);
    if (pages != m_printPagesWide) {
        m_printPagesWide = pages;
        emit printSetupChanged();
    }
}

namespace {

::QindaOffice::Plan::GanttPrintOptions printOptions(const PlanHost &host, const PlanService &service)
{
    namespace Engine = ::QindaOffice::Plan;
    Engine::GanttPrintOptions o;
    o.from = QDate::fromString(host.printFrom(), Qt::ISODate);
    o.to = QDate::fromString(host.printTo(), Qt::ISODate);
    o.pagesWide = host.printPagesWide();
    o.style = service.ganttStyle(); // M79: Gantt, Tracking or Leveling Gantt
    o.showSlack = service.showSlack();
    QStringList columns = service.tableFields();
    columns.removeAll(QStringLiteral("indicators"));
    columns.removeAll(QStringLiteral("notes"));
    columns.removeAll(QStringLiteral("mode"));
    columns.prepend(QStringLiteral("id"));
    o.columns = columns.mid(0, 6);
    o.showCritical = service.showCritical();
    o.baseline = service.showBaseline() || o.style == QLatin1String("tracking") ? service.ganttBaseline() : -1;
    o.showBarText = service.showBarText();
    o.today = Engine::toMinutes(service.todayDate(), 12 * 60);
    o.statusDate = service.project().info.statusDate;
    o.locale = service.locale();
    // M79: exactly the rows the view shows — sorted, filtered, collapsed.
    o.rows = service.visibleRows();
    return o;
}

::QindaOffice::Plan::NetworkPrintOptions networkOptions(const PlanHost &host, const PlanService &service)
{
    ::QindaOffice::Plan::NetworkPrintOptions o;
    o.rows = service.visibleRows();
    o.pagesWide = host.printPagesWide();
    o.showCritical = service.showCritical();
    o.statusDate = service.project().info.statusDate;
    o.locale = service.locale();
    return o;
}

} // namespace

Quick::PrintRenderCallback PlanHost::printRenderCallback() const
{
    namespace Engine = ::QindaOffice::Plan;
    if (m_view == QLatin1String("reports")) {
        const Engine::PlanReport report = Engine::buildReport(m_service->project(), m_reportId, m_service->todayDate(),
                                                              m_service->locale());
        return Engine::asPrintCallback<QPrinter>(Engine::reportPainter(report, m_service->locale()));
    }
    if (m_view == QLatin1String("network")) {
        return Engine::asPrintCallback<QPrinter>(Engine::networkPainter(m_service->project(), networkOptions(*this, *m_service)));
    }
    return Engine::asPrintCallback<QPrinter>(Engine::ganttPainter(m_service->project(), printOptions(*this, *m_service)));
}

bool PlanHost::printPrefersLandscape() const
{
    return m_view != QLatin1String("reports");
}

int PlanHost::ganttPageCount() const
{
    namespace Engine = ::QindaOffice::Plan;
    const QPageSize size = Engine::defaultPageSize(m_service->locale());
    const QSizeF points = size.size(QPageSize::Point);
    const QSizeF landscape(points.height(), points.width());
    if (m_view == QLatin1String("network")) {
        return Engine::networkPageCount(m_service->project(), networkOptions(*this, *m_service), landscape);
    }
    return Engine::ganttPageCount(m_service->project(), printOptions(*this, *m_service), landscape);
}

QVariantMap PlanHost::exportPdfTo(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!exportPdf(path)) {
        return PlanService::fail(tr("The PDF could not be written to %1.").arg(path));
    }
    return PlanService::ok({{QStringLiteral("path"), path}});
}

} // namespace QindaOffice::Apps::Plan
