// SPDX-License-Identifier: GPL-3.0-or-later
// Calendar and tasks in the service: local events created, updated and
// deleted; the projection of a range with a weekly rule and an exception;
// CalendarGridView shift/resize proposals in every scope; calendar
// visibility; an .ics export → import round trip through the toolkit's
// IcsCalendarProvider; recurrence preview; the task board.

#include "mail_test_support.h"
#include "qindamail/src/ics_tasks.h"

#include <qindatk/local_productivity_store.h>

#include <QFile>

using namespace QindaOffice::Apps::Mail;

class TestMailCalendar : public QObject {
    Q_OBJECT

    static QVariantList rowsOf(const MailService *service, const QString &eventId)
    {
        QVariantList rows;
        for (const QVariant &value : service->events()) {
            if (value.toMap().value(QStringLiteral("eventId")).toString() == eventId) rows.append(value);
        }
        return rows;
    }

    static QStringList startsOf(const MailService *service, const QString &eventId)
    {
        QStringList starts;
        for (const QVariant &value : rowsOf(service, eventId))
            starts.append(value.toMap().value(QStringLiteral("startLocal")).toString());
        starts.sort();
        return starts;
    }

    static QVariantMap weekly()
    {
        return {{QStringLiteral("frequency"), QStringLiteral("weekly")},
                {QStringLiteral("interval"), 1},
                {QStringLiteral("weekdays"), QVariantList{1}},
                {QStringLiteral("endMode"), QStringLiteral("never")}};
    }

    // A weekly Monday 09:00–10:00 standup from 7 Sep 2026 in Berlin.
    static QString createStandup(MailService *service, QSignalSpy &spy)
    {
        const QString request = service->createEvent({{QStringLiteral("id"), QStringLiteral("standup")},
                                                      {QStringLiteral("title"), QStringLiteral("Standup")},
                                                      {QStringLiteral("startLocal"), QStringLiteral("2026-09-07T09:00")},
                                                      {QStringLiteral("endLocal"), QStringLiteral("2026-09-07T10:00")},
                                                      {QStringLiteral("zone"), QStringLiteral("Europe/Berlin")},
                                                      {QStringLiteral("recurrenceRule"), weekly()}});
        return MailTest::waitState(spy, request).first;
    }

private slots:
    void createProjectUpdateDelete()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QSignalSpy changed(service, &MailService::eventChangeFinished);
        QCOMPARE(createStandup(service, changed), QStringLiteral("accepted"));
        QVERIFY(service->projectRange(QStringLiteral("2026-09-07"), QStringLiteral("2026-10-05")));
        QCOMPARE(startsOf(service, QStringLiteral("standup")),
                 (QStringList{QStringLiteral("2026-09-07T09:00:00"), QStringLiteral("2026-09-14T09:00:00"),
                              QStringLiteral("2026-09-21T09:00:00"), QStringLiteral("2026-09-28T09:00:00")}));
        const QVariantMap row = rowsOf(service, QStringLiteral("standup")).first().toMap();
        QCOMPARE(row.value(QStringLiteral("calendarId")).toString(), QStringLiteral("local"));
        QCOMPARE(row.value(QStringLiteral("calendarName")).toString(), QStringLiteral("Calendar"));
        QCOMPARE(row.value(QStringLiteral("readOnly")).toBool(), false);

        // One occurrence excluded (an exception) drops out of the projection.
        QString request = service->deleteEvent(QStringLiteral("standup"),
                                               QStringLiteral("standup/2026-09-14T09:00:00"), QStringLiteral("occurrence"));
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("accepted"));
        QTRY_COMPARE(startsOf(service, QStringLiteral("standup")).size(), 3);
        QVERIFY(!startsOf(service, QStringLiteral("standup")).contains(QStringLiteral("2026-09-14T09:00:00")));

        // Title change of the series; a bad time range is refused.
        request = service->updateEvent({{QStringLiteral("id"), QStringLiteral("standup")},
                                        {QStringLiteral("title"), QStringLiteral("Daily sync")}});
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("accepted"));
        QCOMPARE(service->eventById(QStringLiteral("standup")).value(QStringLiteral("title")).toString(),
                 QStringLiteral("Daily sync"));
        request = service->updateEvent({{QStringLiteral("id"), QStringLiteral("standup")},
                                        {QStringLiteral("endLocal"), QStringLiteral("2026-09-07T08:00")}});
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("rejected"));

        // "This and following" ends the series before the occurrence.
        request = service->deleteEvent(QStringLiteral("standup"), QStringLiteral("standup/2026-09-28T09:00:00"),
                                       QStringLiteral("following"));
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("accepted"));
        QTRY_COMPARE(startsOf(service, QStringLiteral("standup")),
                     (QStringList{QStringLiteral("2026-09-07T09:00:00"), QStringLiteral("2026-09-21T09:00:00")}));

        request = service->deleteEvent(QStringLiteral("standup"), QString(), QStringLiteral("series"));
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("accepted"));
        QTRY_VERIFY(rowsOf(service, QStringLiteral("standup")).isEmpty());
        QVERIFY(service->eventById(QStringLiteral("standup")).isEmpty());
    }

    void gridProposals()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QSignalSpy changed(service, &MailService::eventChangeFinished);
        QCOMPARE(createStandup(service, changed), QStringLiteral("accepted"));
        QVERIFY(service->projectRange(QStringLiteral("2026-09-07"), QStringLiteral("2026-10-05")));
        int serial = 0;
        const auto propose = [&](const QString &kind, int minutes, const QString &scope, const QString &occurrence) {
            const QString request = QStringLiteral("grid-%1").arg(++serial);
            service->applyChange(request, QStringLiteral("standup"),
                                 {{QStringLiteral("kind"), kind},
                                  {QStringLiteral("minutes"), minutes},
                                  {QStringLiteral("zone"), QStringLiteral("Europe/Berlin")},
                                  {QStringLiteral("scope"), scope},
                                  {QStringLiteral("occurrenceId"), occurrence}});
            return MailTest::waitState(changed, request).first;
        };
        // One occurrence 30 minutes later: an override under the same id.
        QCOMPARE(propose(QStringLiteral("shift-local"), 30, QStringLiteral("occurrence"),
                         QStringLiteral("standup/2026-09-14T09:00:00")),
                 QStringLiteral("accepted"));
        QTRY_VERIFY(startsOf(service, QStringLiteral("standup")).contains(QStringLiteral("2026-09-14T09:30:00")));
        // Resize that occurrence by +15: it keeps the moved start.
        QCOMPARE(propose(QStringLiteral("resize-local"), 15, QStringLiteral("occurrence"),
                         QStringLiteral("standup/2026-09-14T09:00:00")),
                 QStringLiteral("accepted"));
        QTRY_VERIFY([&] {
            for (const QVariant &value : rowsOf(service, QStringLiteral("standup"))) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("occurrenceId")).toString() == QLatin1String("standup/2026-09-14T09:00:00"))
                    return row.value(QStringLiteral("endLocal")).toString() == QLatin1String("2026-09-14T10:45:00");
            }
            return false;
        }());
        // Resize the series: every other occurrence ends at 10:30.
        QCOMPARE(propose(QStringLiteral("resize-local"), 30, QStringLiteral("series"), QString()),
                 QStringLiteral("accepted"));
        QCOMPARE(service->eventById(QStringLiteral("standup")).value(QStringLiteral("endLocal")).toString(),
                 QStringLiteral("2026-09-07T10:30:00"));
        // Shift "this and following" from 21 Sep by an hour: a split series.
        QCOMPARE(propose(QStringLiteral("shift-local"), 60, QStringLiteral("following"),
                         QStringLiteral("standup/2026-09-21T09:00:00")),
                 QStringLiteral("accepted"));
        QTRY_VERIFY(!service->eventById(QStringLiteral("standup~20260921")).isEmpty());
        QCOMPARE(service->eventById(QStringLiteral("standup~20260921")).value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-09-21T10:00:00"));
        // Resize "following" from 28 Sep on the tail: a second split, the
        // earlier weeks untouched.
        const QString tail = QStringLiteral("standup~20260921");
        service->applyChange(QStringLiteral("grid-tail"), tail,
                             {{QStringLiteral("kind"), QStringLiteral("resize-local")},
                              {QStringLiteral("minutes"), 60},
                              {QStringLiteral("scope"), QStringLiteral("following")},
                              {QStringLiteral("occurrenceId"), tail + QStringLiteral("/2026-09-28T10:00:00")}});
        QCOMPARE(MailTest::waitState(changed, QStringLiteral("grid-tail")).first, QStringLiteral("accepted"));
        const QVariantMap split = service->eventById(tail + QStringLiteral("~20260928"));
        QVERIFY(!split.isEmpty());
        QCOMPARE(split.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-09-28T10:00:00"));
        QCOMPARE(split.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-09-28T12:30:00"));
        QCOMPARE(service->eventById(tail).value(QStringLiteral("endLocal")).toString(),
                 QStringLiteral("2026-09-21T11:30:00"));
        // Nonsense is refused: an unknown kind, an end before the start.
        QCOMPARE(propose(QStringLiteral("teleport"), 5, QStringLiteral("series"), QString()), QStringLiteral("rejected"));
        QCOMPARE(propose(QStringLiteral("resize-local"), -600, QStringLiteral("series"), QString()),
                 QStringLiteral("rejected"));
    }

    void visibilityAndPreview()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QSignalSpy changed(service, &MailService::eventChangeFinished);
        QCOMPARE(createStandup(service, changed), QStringLiteral("accepted"));
        QVERIFY(service->projectRange(QStringLiteral("2026-09-07"), QStringLiteral("2026-09-14")));
        QVERIFY(!service->events().isEmpty());
        service->setCalendarVisible(QStringLiteral("local"), false);
        QVERIFY(service->events().isEmpty());
        QVERIFY(!service->calendars().first().toMap().value(QStringLiteral("visible")).toBool());
        QVERIFY(env.reopen()->calendars().constLast().toMap().value(QStringLiteral("visible")).toBool() == false);
        service->setCalendarVisible(QStringLiteral("local"), true);
        QVERIFY(!service->events().isEmpty());

        QVariantMap everyTwo = weekly();
        everyTwo.insert(QStringLiteral("interval"), 2);
        const QVariantList preview = service->recurrencePreview(QStringLiteral("standup"), everyTwo);
        QVERIFY(preview.size() >= 3);
        QCOMPARE(preview.at(1).toMap().value(QStringLiteral("localStart")).toString().left(10),
                 QStringLiteral("2026-09-21"));
        QVERIFY(!service->projectRange(QStringLiteral("2026-09-14"), QStringLiteral("2026-09-07")));
    }

    void icsRoundTrip()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QSignalSpy changed(service, &MailService::eventChangeFinished);
        QSignalSpy finished(service, &MailService::operationFinished);
        QCOMPARE(createStandup(service, changed), QStringLiteral("accepted"));
        QString request = service->deleteEvent(QStringLiteral("standup"),
                                               QStringLiteral("standup/2026-09-14T09:00:00"), QStringLiteral("occurrence"));
        QCOMPARE(MailTest::waitState(changed, request).first, QStringLiteral("accepted"));
        const QString path = env.dir.filePath(QStringLiteral("export.ics"));
        request = service->exportIcs(QUrl::fromLocalFile(path), QString());
        QCOMPARE(MailTest::waitState(finished, request).first, QStringLiteral("accepted"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("RRULE:FREQ=WEEKLY"));
        QVERIFY(text.contains("EXDATE"));

        // A fresh tree imports it back with the rule and the exception.
        MailTest::Env other;
        QSignalSpy imported(other.service.get(), &MailService::operationFinished);
        request = other.service->importIcs(QUrl::fromLocalFile(path));
        QCOMPARE(MailTest::waitState(imported, request).first, QStringLiteral("accepted"));
        QVERIFY(other.service->projectRange(QStringLiteral("2026-09-07"), QStringLiteral("2026-09-28")));
        QStringList starts;
        for (const QVariant &value : other.service->events())
            starts.append(value.toMap().value(QStringLiteral("startLocal")).toString());
        starts.sort();
        QCOMPARE(starts, (QStringList{QStringLiteral("2026-09-07T09:00:00"), QStringLiteral("2026-09-21T09:00:00")}));
        QCOMPARE(other.service->events().first().toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Standup"));
        // Importing the same file again changes nothing.
        request = other.service->importIcs(QUrl::fromLocalFile(path));
        QCOMPARE(MailTest::waitState(imported, request).first, QStringLiteral("accepted"));
        QCOMPARE(other.service->store()->events().size(), 1);
    }

    void tasks()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString a = service->addTask(QStringLiteral("Write report"));
        const QString b = service->addTask(QStringLiteral("File taxes"));
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QVERIFY(service->addTask(QStringLiteral("   ")).isEmpty());
        QVariantList columns = service->taskColumns();
        QCOMPARE(columns.size(), 3);
        QCOMPARE(columns.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
        QCOMPARE(columns.at(0).toMap().value(QStringLiteral("tasks")).toList().size(), 2);
        QCOMPARE(columns.at(0).toMap().value(QStringLiteral("tasks")).toList().at(0).toMap()
                     .value(QStringLiteral("taskId")).toString(), a);

        QSignalSpy finished(service, &MailService::operationFinished);
        service->moveTask(QStringLiteral("move-1"), b, QStringLiteral("doing"), 0);
        QCOMPARE(MailTest::waitState(finished, QStringLiteral("move-1")).first, QStringLiteral("accepted"));
        columns = service->taskColumns();
        QCOMPARE(columns.at(0).toMap().value(QStringLiteral("tasks")).toList().size(), 1);
        QCOMPARE(columns.at(1).toMap().value(QStringLiteral("tasks")).toList().at(0).toMap()
                     .value(QStringLiteral("taskId")).toString(), b);
        const QString update = service->updateTask({{QStringLiteral("taskId"), b},
                                                    {QStringLiteral("priority"), QStringLiteral("High")},
                                                    {QStringLiteral("status"), QStringLiteral("done")}});
        QCOMPARE(MailTest::waitState(finished, update).first, QStringLiteral("accepted"));
        QCOMPARE(service->taskColumns().at(2).toMap().value(QStringLiteral("tasks")).toList().at(0).toMap()
                     .value(QStringLiteral("priority")).toString(), QStringLiteral("High"));
        service->moveTask(QStringLiteral("move-2"), QStringLiteral("missing"), QStringLiteral("done"), 0);
        QCOMPARE(MailTest::waitState(finished, QStringLiteral("move-2")).first, QStringLiteral("rejected"));
        const QString removed = service->deleteTask(a);
        QCOMPARE(MailTest::waitState(finished, removed).first, QStringLiteral("accepted"));
        QCOMPARE(env.reopen()->store()->tasks().size(), 1);
    }

    // M56 (D-061): QindaNote hands action items over as an .ics of VTODOs;
    // they land on the task board with their due dates and notes (the
    // calendar import alone skips tasks).
    void icsTasksBecomeTasks()
    {
        const QByteArray ics =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VTODO\r\nUID:a\r\nSUMMARY:Read\\, carefully\\; chapter 4\r\nDUE;VALUE=DATE:20261002\r\n"
            "DESCRIPTION:for the quiz\\nFrom: Biology\r\nPRIORITY:1\r\nEND:VTODO\r\n"
            "BEGIN:VTODO\r\nUID:b\r\nSUMMARY:Send the sli\r\n des\r\nEND:VTODO\r\n"
            "BEGIN:VEVENT\r\nUID:c\r\nSUMMARY:Not a task\r\nDTSTART;VALUE=DATE:20261002\r\nEND:VEVENT\r\n"
            "END:VCALENDAR\r\n";
        const auto parsed = QindaOffice::Apps::Mail::parseIcsTasks(ics);
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed.first().title, QStringLiteral("Read, carefully; chapter 4"));
        QCOMPARE(parsed.first().due, QDate(2026, 10, 2));
        QCOMPARE(parsed.first().description, QStringLiteral("for the quiz\nFrom: Biology"));
        QCOMPARE(parsed.first().priority, QStringLiteral("High"));
        QCOMPARE(parsed.at(1).title, QStringLiteral("Send the slides")); // folded line
        MailTest::Env env;
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("items.ics")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(ics);
        file.close();
        QCOMPARE(env.service->importIcsTasks(file.fileName()), 2);
        bool found = false;
        for (const QVariant &value : env.service->store()->tasks()) {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("title")).toString() == QLatin1String("Read, carefully; chapter 4")) {
                found = true;
                QCOMPARE(task.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-10-02"));
                QCOMPARE(task.value(QStringLiteral("priority")).toString(), QStringLiteral("High"));
            }
        }
        QVERIFY(found);
    }
};

MAIL_TEST_MAIN(TestMailCalendar)

#include "tst_qindamail_calendar.moc"
