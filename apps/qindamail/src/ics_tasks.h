// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VTODO components of an iCalendar file (M56, D-061). QindaTK's ICS import
// deliberately takes only events into a calendar; tasks belong on the task
// board. QindaNote hands its action items over as an .ics file of VTODOs
// (plus VEVENTs on their due dates), so MailService::importIcs also turns
// the VTODOs into tasks. Pure: no store, no files.

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>

namespace QindaOffice::Apps::Mail {

struct IcsTask {
    QString title;
    QString description;
    QDate due;
    QString priority; // "High" / "Normal" / "Low"
    bool done = false;
};

[[nodiscard]] QList<IcsTask> parseIcsTasks(const QByteArray &ics);

} // namespace QindaOffice::Apps::Mail
