// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the document, the view, and the print source.
#include "plan_host.h"

#include "gantt_print.h"
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
    return {QStringLiteral("gantt"), QStringLiteral("sheet"),    QStringLiteral("resources"),
            QStringLiteral("usage"), QStringLiteral("calendar"), QStringLiteral("reports")};
}

void PlanHost::setCurrentView(const QString &view)
{
    if (!viewKeys().contains(view)) {
        return;
    }
    if (m_view != view) {
        m_view = view;
        emit currentViewChanged();
        setRibbonContexts(view == QLatin1String("gantt") ? QStringList{QStringLiteral("ganttFormat")} : QStringList{});
        syncEnablement();
    }
    static const QHash<QString, QString> ids{{QStringLiteral("gantt"), QStringLiteral("planViewGanttAction")},
                                             {QStringLiteral("sheet"), QStringLiteral("planViewTaskSheetAction")},
                                             {QStringLiteral("resources"), QStringLiteral("planViewResourceSheetAction")},
                                             {QStringLiteral("usage"), QStringLiteral("planViewResourceUsageAction")},
                                             {QStringLiteral("calendar"), QStringLiteral("planViewCalendarAction")},
                                             {QStringLiteral("reports"), QStringLiteral("planViewReportsAction")}};
    if (Quick::ActionEntry *entry = action(ids.value(view))) {
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
    QStringList columns = service.tableFields();
    columns.removeAll(QStringLiteral("indicators"));
    columns.removeAll(QStringLiteral("notes"));
    columns.removeAll(QStringLiteral("mode"));
    columns.prepend(QStringLiteral("id"));
    o.columns = columns.mid(0, 6);
    o.showCritical = service.showCritical();
    o.baseline = service.showBaseline() ? 0 : -1;
    o.showBarText = service.showBarText();
    o.today = Engine::toMinutes(service.todayDate(), 12 * 60);
    o.statusDate = service.project().info.statusDate;
    o.locale = service.locale();
    const Engine::Project &p = service.project();
    for (const Engine::Task &t : p.tasks) {
        if (t.parentIndex >= 0) {
            // Hidden when any ancestor is collapsed.
            for (int a = t.parentIndex; a >= 0; a = p.tasks.at(a).parentIndex) {
                if (service.isCollapsed(p.tasks.at(a).uid)) {
                    o.hiddenTaskUids.append(t.uid);
                    break;
                }
            }
        }
    }
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
    return Engine::ganttPageCount(m_service->project(), printOptions(*this, *m_service),
                                  QSizeF(points.height(), points.width()));
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
