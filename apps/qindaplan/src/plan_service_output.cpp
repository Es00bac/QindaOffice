// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: reports, files and the clipboard (plan_service.h).
#include "atomic_file_store.h"
#include "mspdi.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_reports.h"
#include "plan_service.h"
#include "plan_time.h"
#include "timeline_render.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QVariantMap written(const QString &path, const Core::AtomicFileStore::Result &r)
{
    if (!r.ok) {
        return PlanService::fail(r.error);
    }
    return PlanService::ok({{QStringLiteral("path"), path}});
}

} // namespace

QVariantMap PlanService::report(const QString &id) const
{
    const Engine::PlanReport r = Engine::buildReport(project(), id, m_today, m_locale);
    QVariantList kpis;
    for (const Engine::ReportKpi &k : r.kpis) {
        kpis.append(QVariantMap{{QStringLiteral("id"), k.id},
                                {QStringLiteral("label"), k.label},
                                {QStringLiteral("value"), k.value},
                                {QStringLiteral("detail"), k.detail},
                                {QStringLiteral("percent"), k.percent},
                                {QStringLiteral("tone"), k.tone}});
    }
    QVariantList tables;
    for (const Engine::ReportTable &t : r.tables) {
        QVariantList rows;
        for (int i = 0; i < t.rows.size(); ++i) {
            const int row = t.rowTaskUids.value(i) > 0 ? project().taskIndex(t.rowTaskUids.at(i)) : -1;
            QVariantMap cells{{QStringLiteral("tone"), t.rowTones.value(i)},
                              {QStringLiteral("taskId"), row >= 0 ? idOfRow(row) : QString()}};
            for (int c = 0; c < t.rows.at(i).size(); ++c) {
                cells.insert(QStringLiteral("c%1").arg(c), t.rows.at(i).at(c));
            }
            rows.append(cells);
        }
        QVariantList numeric;
        for (int c : t.numericColumns) {
            numeric.append(c);
        }
        tables.append(QVariantMap{{QStringLiteral("id"), t.id},
                                  {QStringLiteral("title"), t.title},
                                  {QStringLiteral("columns"), t.columns},
                                  {QStringLiteral("numericColumns"), numeric},
                                  {QStringLiteral("rows"), rows},
                                  {QStringLiteral("emptyText"), t.emptyText}});
    }
    return {{QStringLiteral("id"), r.id},
            {QStringLiteral("title"), r.title},
            {QStringLiteral("subtitle"), r.subtitle},
            {QStringLiteral("kpis"), kpis},
            {QStringLiteral("tables"), tables}};
}

QVariantMap PlanService::exportCsv(const QUrl &url) const
{
    QStringList fields = tableFields();
    fields.removeAll(QStringLiteral("indicators"));
    fields.prepend(QStringLiteral("id"));
    const QString path = localPath(url);
    return written(path, Core::AtomicFileStore::write(path, Engine::taskSheetCsv(project(), fields, m_locale)));
}

QVariantMap PlanService::exportMspdi(const QUrl &url) const
{
    const QString path = localPath(url);
    return written(path, Core::AtomicFileStore::write(path, Engine::writeMspdi(project())));
}

QVariantMap PlanService::exportTimelinePng(const QUrl &url, int width, int height) const
{
    const QString path = localPath(url);
    Engine::TimelineOptions options;
    options.today = Engine::toMinutes(m_today, 12 * 60);
    options.locale = m_locale;
    QString error;
    if (!Engine::exportTimelinePng(project(), path, QSize(std::clamp(width, 320, 8000), std::clamp(height, 120, 4000)),
                                   options, &error)) {
        return fail(error);
    }
    return ok({{QStringLiteral("path"), path}});
}

QVariantMap PlanService::importMspdi(const QUrl &url)
{
    const QString path = localPath(url);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(file.errorString());
    }
    Engine::Project imported;
    const Engine::MspdiResult r = Engine::readMspdi(file.readAll(), &imported);
    if (!r.ok) {
        return fail(r.error);
    }
    m_document->replace(QStringLiteral("Import Microsoft Project XML"), imported);
    if (!r.warnings.isEmpty()) {
        emit notify(QStringLiteral("Imported with notes"), r.warnings.join(QLatin1Char('\n')), QStringLiteral("warning"));
    }
    return ok({{QStringLiteral("warnings"), r.warnings}, {QStringLiteral("tasks"), int(imported.tasks.size())}});
}

QVariantMap PlanService::copySelection() const
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to copy."));
    }
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(Engine::kTasksMimeType), Engine::copyTasks(project(), rows));
    mime->setText(Engine::copyTasksText(project(), rows, m_locale));
    QGuiApplication::clipboard()->setMimeData(mime);
    return ok({{QStringLiteral("count"), int(rows.size())}});
}

QVariantMap PlanService::cutSelection()
{
    const QVariantMap copied = copySelection();
    if (!copied.value(QStringLiteral("ok")).toBool()) {
        return copied;
    }
    return deleteSelectedTasks();
}

QVariantMap PlanService::paste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr) {
        return fail(QStringLiteral("The clipboard is empty."));
    }
    const QList<int> rows = selectedRows();
    const int at = rows.isEmpty() ? int(project().tasks.size()) : rows.first();
    const QString format = QString::fromLatin1(Engine::kTasksMimeType);
    QString first;
    QVariantMap r;
    if (mime->hasFormat(format)) {
        const QByteArray data = mime->data(format);
        r = apply(QStringLiteral("Paste Tasks"), [&](Engine::Project &p) {
            const QList<int> pasted = Engine::pasteTasks(p, at, data);
            if (pasted.isEmpty()) {
                return QStringLiteral("The clipboard holds no tasks.");
            }
            first = p.tasks.at(pasted.first()).guid.toString(QUuid::WithoutBraces);
            return QString();
        });
    } else if (mime->hasText()) {
        // Plain text: one task per line, the first column its name and a
        // second column, when it reads as one, its duration.
        const QStringList lines = mime->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QLocale locale = m_locale;
        r = apply(QStringLiteral("Paste Tasks"), [&](Engine::Project &p) {
            int index = at;
            for (const QString &line : lines) {
                const QStringList cells = line.split(QLatin1Char('\t'));
                const QString name = cells.value(0).trimmed();
                if (name.isEmpty()) {
                    continue;
                }
                const int row = Engine::addTask(p, index++, Engine::NewTaskKind::Task, name);
                if (first.isEmpty()) {
                    first = p.tasks.at(row).guid.toString(QUuid::WithoutBraces);
                }
                if (cells.size() > 1) {
                    (void)Engine::setTaskField(p, row, QStringLiteral("duration"), cells.at(1), locale);
                }
            }
            return first.isEmpty() ? QStringLiteral("The clipboard holds no text lines.") : QString();
        });
    } else {
        return fail(QStringLiteral("The clipboard holds nothing QindaPlan can paste."));
    }
    if (r.value(QStringLiteral("ok")).toBool() && !first.isEmpty()) {
        setCurrentTaskId(first);
        emit revealTask(first);
    }
    return r;
}

} // namespace QindaOffice::Apps::Plan
