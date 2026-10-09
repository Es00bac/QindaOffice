// SPDX-License-Identifier: GPL-3.0-or-later
// MailService (M76, D-085): an Outlook .msg that is not mail — a contact, a
// contact group (IPM.DistList), an appointment or a task — opened into
// People, the local calendar or the task board, through the mapping .msg
// and .pst share (outlook_items.h, outlook_records.h). The .pst import is
// pst_import.cpp.
#include "mail_service.h"

#include "contact_store.h"
#include "outlook_items.h"
#include "outlook_records.h"

#include <qindatk/local_productivity_store.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace QindaOffice::Apps::Mail {

using namespace QindaOffice::Mail;

QVariantMap MailService::importOutlookItemFile(const QString &path)
{
    QVariantMap result{{QStringLiteral("handled"), false}};
    const QString local = path.startsWith(QStringLiteral("file:")) ? QUrl(path).toLocalFile() : path;
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024
        || !file.peek(8).startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1")))
        return result;
    const QByteArray bytes = file.readAll();
    OutlookItem item;
    QString error;
    if (!readOutlookMsg(bytes, &item, &error)) return result;   // importMessageFile reports it
    // The same file opened twice is the same contact, event or task.
    const QString key = QStringLiteral("msg:")
        + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(24));
    const QString name = QFileInfo(local).fileName();
    QString view;
    QString id;
    bool ok = false;
    switch (item.kind) {
    case OutlookItemKind::Contact:
    case OutlookItemKind::DistList: {
        item.contact.uid = OutlookRecords::uidFor(key);
        if (item.kind == OutlookItemKind::DistList)
            item.contact.members = OutlookRecords::groupMembers(item.members, {}, m_contacts);
        const QVariantMap merged = m_contacts->importContacts({item.contact});
        ok = merged.value(QStringLiteral("ok")).toBool();
        error = merged.value(QStringLiteral("error")).toString();
        view = QStringLiteral("people");
        break;
    }
    case OutlookItemKind::Appointment: {
        QVariantMap record;
        id = key;
        ok = OutlookRecords::eventRecord(item.appointment, id, &record)
            && m_store->upsert(QStringLiteral("events"), record).value(QStringLiteral("ok")).toBool() && saveStore(&error);
        if (!ok && error.isEmpty()) error = tr("%1 has no date").arg(name);
        view = QStringLiteral("calendar");
        break;
    }
    case OutlookItemKind::Task: {
        id = QStringLiteral("task:") + key;
        const QString status = OutlookRecords::taskStatus(item.task);
        int order = 0;
        for (const QVariant &value : m_store->tasks()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("id")).toString() == id) {
                order = task.value(QStringLiteral("order")).toInt();
                break;
            }
            if (task.value(QStringLiteral("status")).toString() == status)
                order = qMax(order, task.value(QStringLiteral("order")).toInt() + 1);
        }
        ok = m_store->upsert(QStringLiteral("tasks"), OutlookRecords::taskRecord(item.task, id, order))
                 .value(QStringLiteral("ok")).toBool() && saveStore(&error);
        view = QStringLiteral("tasks");
        break;
    }
    case OutlookItemKind::Mail:
    case OutlookItemKind::Other:
        return result;
    }
    if (ok) refreshDerived();
    emit notify(ok ? (view == QLatin1String("people") ? tr("Contact imported")
                      : view == QLatin1String("calendar") ? tr("Event added to your calendar") : tr("Task added"))
                   : tr("Could not open %1").arg(name),
                ok ? name : error, ok ? QStringLiteral("success") : QStringLiteral("danger"));
    return {{QStringLiteral("handled"), true}, {QStringLiteral("ok"), ok}, {QStringLiteral("view"), view},
            {QStringLiteral("id"), id}, {QStringLiteral("error"), error}};
}

} // namespace QindaOffice::Apps::Mail
