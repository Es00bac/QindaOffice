// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to mspdi_reader*.cpp: the streaming MSPDI reader. One pass over
// the document fills Mspdi::Raw (mspdi_p.h); every child element it does not
// model is captured verbatim (mspdi_keep.h) instead of skipped.
//
// AGENT-NOTE: `in()` is the stream being read — the document, or a fragment
// the reader captured first and parses second (a Baseline, an Exception),
// so the same code reads both and nothing has to be read twice from the
// document.

#include "mspdi.h"
#include "mspdi_keep.h"
#include "mspdi_p.h"

#include <QHash>
#include <QXmlStreamReader>

namespace QindaOffice::Plan::Mspdi {

class Reader {
public:
    explicit Reader(const QByteArray &xml)
        : m_root(xml)
    {
    }
    MspdiResult run(Project *out);

private:
    QXmlStreamReader &in() { return *m_in; }
    QString text() { return in().readElementText(QXmlStreamReader::SkipChildElements).trimmed(); }
    int integer(int fallback)
    {
        bool ok = false;
        const int v = text().toInt(&ok);
        return ok ? v : fallback;
    }
    bool boolean()
    {
        const QString t = text();
        return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    qint64 scaled(int scale, qint64 fallback) { return scaledDecimal(text(), scale).value_or(fallback); }
    Minutes duration() { return parseWireDuration(text()).value_or(0); }
    void warn(const QString &message) { m_raw.warn(message); }
    // Captures the current element into `kept`; `after` is the element
    // before it in the file.
    void keep(QList<Kept> &kept, Kind kind, const QString &after)
    {
        const QString name = in().name().toString();
        kept << Kept{name, after, capture(in()), isDerived(kind, name)};
    }
    // Reads `fragment` with `parse` as if it were the current element.
    template<class Parse>
    void reparse(const QString &fragment, Parse &&parse)
    {
        QXmlStreamReader sub(fragment);
        QXmlStreamReader *saved = m_in;
        m_in = &sub;
        if (sub.readNextStartElement()) {
            parse();
        }
        m_in = saved;
    }
    // The stamp of an object as read (mspdi_keep.h): {element: text}.
    static QJsonObject stampOf(Kind kind, const QHash<QString, QString> &parts);

    void readProject();
    void readCalendar();
    DayHours readWorkingTimes();
    void readWeekDay(Calendar &calendar);
    bool readException(Calendar &calendar);
    bool readTimePeriod(QDate *from, QDate *to);
    void readTask();
    void readLink(RawTask &raw);
    // M79: the current <Baseline>, parsed and kept as written (mspdi_p.h).
    RawBaseline readBaseline();
    void readResource();
    void readAssignment();

    QXmlStreamReader m_root;
    QXmlStreamReader *m_in = &m_root;
    Raw m_raw;
    ProjectInfo &m_info = m_raw.info;
    QList<Calendar> &m_calendars = m_raw.calendars;
    QList<RawTask> &m_tasks = m_raw.tasks;
    QList<RawResource> &m_resources = m_raw.resources;
    QList<RawAssignment> &m_assignments = m_raw.assignments;
};

} // namespace QindaOffice::Plan::Mspdi
