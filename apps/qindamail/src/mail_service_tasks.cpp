// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: the task board (Tk.TaskBoard columns over the store's
// `tasks`). Tasks of a CalDAV account (VTODO, remoteAccount set) are
// written through its calendar provider after the local save.
#include "mail_service.h"

#include "provider_calls.h"

#include <qindatk/local_productivity_store.h>

#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace QindaOffice::Apps::Mail {

namespace {

QVariantMap findTask(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    }
    return {};
}

} // namespace

void MailService::rebuildTasks()
{
    const QList<QPair<QString, QString>> columns{{QStringLiteral("todo"), tr("To do")},
                                                 {QStringLiteral("doing"), tr("In progress")},
                                                 {QStringLiteral("done"), tr("Done")}};
    QList<QVariantMap> tasks;
    for (const QVariant &value : m_store->tasks()) tasks.append(value.toMap());
    std::stable_sort(tasks.begin(), tasks.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("order")).toInt() < b.value(QStringLiteral("order")).toInt();
    });
    QVariantList result;
    for (const auto &[status, label] : columns) {
        QVariantList cards;
        for (const QVariantMap &task : std::as_const(tasks)) {
            QString taskStatus = task.value(QStringLiteral("status")).toString();
            if (taskStatus.isEmpty() || taskStatus == QLatin1String("cancelled")) taskStatus = QStringLiteral("todo");
            if (taskStatus != status) continue;
            cards.append(QVariantMap{{QStringLiteral("taskId"), task.value(QStringLiteral("id"))},
                                     {QStringLiteral("title"), task.value(QStringLiteral("title"))},
                                     {QStringLiteral("assignee"), task.value(QStringLiteral("assignee"))},
                                     {QStringLiteral("dueDate"), task.value(QStringLiteral("dueDate"))},
                                     {QStringLiteral("priority"), task.value(QStringLiteral("priority"))},
                                     {QStringLiteral("syncState"), task.value(QStringLiteral("syncState"))},
                                     {QStringLiteral("accountId"), ownerAccountOf(task)}});
        }
        result.append(QVariantMap{{QStringLiteral("status"), status}, {QStringLiteral("label"), label},
                                  {QStringLiteral("tasks"), cards}});
    }
    if (result == m_taskColumns) return;
    m_taskColumns = result;
    emit taskColumnsChanged();
}

QString MailService::addTask(const QString &title)
{
    if (title.trimmed().isEmpty()) return {};
    int order = 0;
    for (const QVariant &value : m_store->tasks()) {
        const QVariantMap task = value.toMap();
        if (task.value(QStringLiteral("status")).toString() == QLatin1String("todo"))
            order = std::max(order, task.value(QStringLiteral("order")).toInt() + 1);
    }
    const QString id = QStringLiteral("task:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QVariantMap task{{QStringLiteral("id"), id},
                           {QStringLiteral("title"), title.trimmed()},
                           {QStringLiteral("status"), QStringLiteral("todo")},
                           {QStringLiteral("order"), order},
                           {QStringLiteral("priority"), QStringLiteral("Normal")},
                           {QStringLiteral("assignee"), QString()},
                           {QStringLiteral("dueDate"), QString()}};
    QString error;
    if (!m_store->upsert(QStringLiteral("tasks"), task).value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        emit notify(tr("The task was not saved"), error, QStringLiteral("danger"));
        return {};
    }
    rebuildTasks();
    return id;
}

void MailService::moveTask(const QString &requestId, const QString &taskId, const QString &toStatus, int index)
{
    const QVariantMap moved = m_store->moveTask(taskId, toStatus, index);
    QString error = moved.value(QStringLiteral("error")).toString();
    if (!moved.value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        QTimer::singleShot(0, this, [this, requestId, error] {
            emit operationFinished(requestId, QStringLiteral("rejected"), error);
        });
        return;
    }
    rebuildTasks();
    writeThrough(QStringLiteral("tasks"), findTask(m_store->tasks(), taskId), requestId, false);
}

QString MailService::updateTask(const QVariantMap &task)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("task"));
    QVariantMap record = findTask(m_store->tasks(), task.value(QStringLiteral("taskId"), task.value(QStringLiteral("id"))).toString());
    QString error;
    if (record.isEmpty()) {
        error = tr("The task no longer exists");
    } else {
        for (const char *key : {"title", "assignee", "dueDate", "priority", "status", "description"}) {
            if (task.contains(QString::fromLatin1(key))) record.insert(QString::fromLatin1(key), task.value(QString::fromLatin1(key)));
        }
        const QVariantMap upserted = m_store->upsert(QStringLiteral("tasks"), record);
        error = upserted.value(QStringLiteral("error")).toString();
        if (upserted.value(QStringLiteral("ok")).toBool() && saveStore(&error)) {
            rebuildTasks();
            writeThrough(QStringLiteral("tasks"), record, requestId, false);
            return requestId;
        }
    }
    QTimer::singleShot(0, this, [this, requestId, error] {
        emit operationFinished(requestId, QStringLiteral("rejected"), error);
    });
    return requestId;
}

QString MailService::deleteTask(const QString &taskId)
{
    const QString batch = beginBatch(QStringLiteral("task"), 1);
    const QVariantMap record = findTask(m_store->tasks(), taskId);
    AccountRuntime *rt = runtime(record.value(QStringLiteral("remoteAccount")).toString());
    const CalendarBinding *caldav = nullptr;
    for (const CalendarBinding &binding : rt != nullptr ? rt->calendars() : QList<CalendarBinding>()) {
        if (binding.service == QLatin1String("caldav") && caldav == nullptr) caldav = &binding;
    }
    if (caldav != nullptr && caldav->provider != nullptr && !record.value(QStringLiteral("remoteHref")).toString().isEmpty()) {
        QObject *provider = caldav->provider;
        rt->run(QStringLiteral("caldav"), provider,
                [provider, taskId](const QString &rid) {
                    bool started = false;
                    QMetaObject::invokeMethod(provider, "removeRemote", Q_RETURN_ARG(bool, started), Q_ARG(QString, rid),
                                              Q_ARG(QString, QStringLiteral("tasks")), Q_ARG(QString, taskId));
                    return started;
                },
                [this, batch](const QVariantMap &result) { batchDone(batch, result); });
        return batch;
    }
    QString error;
    const bool ok = m_store->remove(QStringLiteral("tasks"), taskId).value(QStringLiteral("ok")).toBool() && saveStore(&error);
    batchDone(batch, ProviderCalls::result({}, ok ? QStringLiteral("accepted") : QStringLiteral("rejected"), error));
    return batch;
}

} // namespace QindaOffice::Apps::Mail
