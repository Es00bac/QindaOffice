// SPDX-License-Identifier: GPL-3.0-or-later
#include "outlook_items.h"
#include "msg_headers.h"
#include "msg_properties.h"
#include "rtf_codepage.h"
namespace QindaOffice::Mail::OutlookDetail {
namespace {
constexpr int kMaxListedDates = 10000;

class Cursor {
public:
    explicit Cursor(const QByteArray &bytes) : m_bytes(bytes) {}
    bool u16(quint16 *out)
    {
        if (m_at + 2 > m_bytes.size()) return false;
        *out = quint16(uchar(m_bytes.at(m_at)) | (uchar(m_bytes.at(m_at + 1)) << 8));
        m_at += 2;
        return true;
    }
    bool u32(quint32 *out)
    {
        quint16 low = 0, high = 0;
        if (!u16(&low) || !u16(&high)) return false;
        *out = quint32(low) | (quint32(high) << 16);
        return true;
    }

    bool bytes(quint32 size, QByteArray *out) {
        if (size > quint32(m_bytes.size() - m_at)) return false;
        *out = m_bytes.mid(m_at, size); m_at += size; return true;
    }
    bool skipBlock(quint32 minimum = 0) {
        quint32 size=0; QByteArray ignored;
        return u32(&size) && size>=minimum && size<=1024*1024 && bytes(size,&ignored);
    }
    bool done() const { return m_at == m_bytes.size(); }
private:
    const QByteArray &m_bytes;
    qsizetype m_at = 0;
};

QDate minutesToDate(quint32 minutes)
{
    return QDate(1601, 1, 1).addDays(minutes / 1440);
}

QList<int> daysFromMask(quint32 mask)
{
    QList<int> days;
    for (int bit = 1; bit <= 6; ++bit) {
        if (mask & (1u << bit)) days.append(bit);   // Monday … Saturday
    }
    if (mask & 1u) days.append(7);                  // Sunday
    return days;
}

// MS-OXOCAL 2.2.1.44.2/.4/.5: all stamps here are floating LOCAL wall time.
QString localMinutes(quint32 value) {
    return QDate(1601,1,1).addDays(value/1440).toString(Qt::ISODate)
        + QLatin1Char('T') + QTime(0,0).addSecs(int(value%1440)*60).toString(QStringLiteral("HH:mm:ss"));
}
bool narrowText(Cursor &c, int codePage, QString *out) {
    quint16 length=0, bytes=0; QByteArray raw;
    if (!c.u16(&length) || !c.u16(&bytes) || length != quint32(bytes)+1 || !c.bytes(bytes,&raw)) return false;
    *out=decodeCodePage(raw,codePage); return true;
}
bool wideText(Cursor &c, QString *out) {
    quint16 length=0; QByteArray raw;
    if (!c.u16(&length) || !c.bytes(quint32(length)*2,&raw)) return false;
    *out=MsgDetail::utf16LeToString(raw); return true;
}
bool readExceptions(Cursor &c, qsizetype expected, int codePage, QVariantList *out) {
    quint32 reader=0,writer=0,startOffset=0,endOffset=0;
    quint16 count=0;
    if (!c.u32(&reader)||reader!=0x3006||!c.u32(&writer)||writer<0x3008||writer>0x3009
        ||!c.u32(&startOffset)||!c.u32(&endOffset)||!c.u16(&count)||count!=expected) return false;
    QList<quint16> flags;
    for (int i=0;i<count;++i) {
        quint32 start=0,end=0,original=0,ignored=0; quint16 flag=0; QString value;
        if (!c.u32(&start)||!c.u32(&end)||!c.u32(&original)||!c.u16(&flag)||(flag&~0x03ff)||end<=start) return false;
        QVariantMap entry{{QStringLiteral("recurrenceId"),localMinutes(original)},
                           {QStringLiteral("startLocal"),localMinutes(start)}, {QStringLiteral("endLocal"),localMinutes(end)}};
        if (flag&0x01) { if(!narrowText(c,codePage,&value))return false;entry.insert(QStringLiteral("title"),value); }
        for (quint16 bit:{quint16(2),quint16(4),quint16(8)}) if ((flag&bit)&&!c.u32(&ignored))return false;
        if (flag&0x10) { if(!narrowText(c,codePage,&value))return false;entry.insert(QStringLiteral("location"),value); }
        for (quint16 bit:{quint16(0x20),quint16(0x40),quint16(0x80),quint16(0x100)}) {
            if ((flag&bit)&&!c.u32(&ignored))return false;
            if(bit==0x80&&(flag&bit))entry.insert(QStringLiteral("allDay"),ignored!=0);
        }
        out->append(entry); flags.append(flag);
    }
    if (!c.skipBlock()) return false;
    for (int i=0;i<count;++i) {
        if (writer>=0x3009 && !c.skipBlock(4)) return false;
        if (!c.skipBlock()) return false;
        const quint16 flag=flags[i];
        if (flag&0x11) {
            quint32 start=0,end=0,original=0; QString value;
            if(!c.u32(&start)||!c.u32(&end)||!c.u32(&original))return false;
            auto entry=out->at(i).toMap();
            if (localMinutes(start)!=entry.value(QStringLiteral("startLocal")).toString()
                ||localMinutes(end)!=entry.value(QStringLiteral("endLocal")).toString()
                ||localMinutes(original)!=entry.value(QStringLiteral("recurrenceId")).toString())return false;
            if(flag&0x01){if(!wideText(c,&value))return false;entry.insert(QStringLiteral("title"),value);}
            if(flag&0x10){if(!wideText(c,&value))return false;entry.insert(QStringLiteral("location"),value);}
            (*out)[i]=entry;
            if(!c.skipBlock())return false;
        }
    }
    return c.skipBlock() && c.done();
}

} // namespace

// MS-OXOCAL 2.2.1.44.1 RecurrencePattern (the start of AppointmentRecurrencePattern).
bool parseOutlookRecurrence(const QByteArray &blob, int codePage, OutlookRecurrence *out)
{
    Cursor c(blob);
    quint16 reader = 0, writer = 0, frequency = 0, pattern = 0, calendar = 0;
    quint32 firstDateTime = 0, period = 0, sliding = 0, day = 0, mask = 0, nth = 0;
    if (!c.u16(&reader) || !c.u16(&writer) || !c.u16(&frequency) || !c.u16(&pattern) || !c.u16(&calendar)
        || !c.u32(&firstDateTime) || !c.u32(&period) || !c.u32(&sliding))
        return false;
    if (reader != 0x3004 || (calendar != 0 && calendar != 1)) return false;   // Gregorian only
    if (pattern == 1 && !c.u32(&mask)) return false;
    if ((pattern == 2 || pattern == 4) && !c.u32(&day)) return false;
    if (pattern == 3 && (!c.u32(&mask) || !c.u32(&nth))) return false;
    if (pattern > 4) return false;   // Hijri patterns
    quint32 endType = 0, count = 0, firstDay = 0, deleted = 0, modified = 0, startDate = 0, endDate = 0;
    if (!c.u32(&endType) || !c.u32(&count) || !c.u32(&firstDay) || !c.u32(&deleted) || deleted > kMaxListedDates)
        return false;
    QList<QDate> deletedDates;
    for (quint32 i = 0; i < deleted; ++i) {
        quint32 minutes = 0;
        if (!c.u32(&minutes)) return false;
        deletedDates.append(minutesToDate(minutes));
    }
    if (!c.u32(&modified) || modified > kMaxListedDates) return false;
    QList<QDate> modifiedDates;
    for (quint32 i = 0; i < modified; ++i) {
        quint32 minutes = 0;
        if (!c.u32(&minutes)) return false;
        modifiedDates.append(minutesToDate(minutes));
    }
    if (!c.u32(&startDate) || !c.u32(&endDate)) return false;

    OutlookRecurrence r;
    r.startDate = minutesToDate(startDate);
    static const char *const kIndexes[] = {"first", "second", "third", "fourth", "last"};
    static const char *const kDays[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
    r.index = nth >= 1 && nth <= 5 ? QString::fromLatin1(kIndexes[nth - 1]) : QString();
    r.firstDayOfWeek = firstDay <= 6 ? QString::fromLatin1(kDays[firstDay]) : QString();
    if (frequency == 0x200A && pattern == 0) {
        r.type = QStringLiteral("daily");
        r.interval = int(qMax<quint32>(1, period / 1440));
    } else if ((frequency == 0x200A || frequency == 0x200B) && pattern == 1) {
        r.type = QStringLiteral("weekly");   // a daily "every weekday" is a weekly Mon–Fri
        r.interval = int(qMax<quint32>(1, period));
        r.days = daysFromMask(mask);
    } else if (frequency == 0x200C && (pattern == 2 || pattern == 4)) {
        r.type = QStringLiteral("absoluteMonthly");
        r.interval = int(qMax<quint32>(1, period));
        r.dayOfMonth = pattern == 4 ? 31 : int(day);
    } else if (frequency == 0x200C && pattern == 3) {
        r.type = QStringLiteral("relativeMonthly");
        r.interval = int(qMax<quint32>(1, period));
        r.days = daysFromMask(mask);
    } else if (frequency == 0x200D && (pattern == 2 || pattern == 4)) {
        r.type = QStringLiteral("absoluteYearly");
        r.interval = int(qMax<quint32>(1, period / 12));
        r.dayOfMonth = pattern == 4 ? 31 : int(day);
        r.month = r.startDate.month();
    } else if (frequency == 0x200D && pattern == 3) {
        r.type = QStringLiteral("relativeYearly");
        r.interval = int(qMax<quint32>(1, period / 12));
        r.days = daysFromMask(mask);
        r.month = r.startDate.month();
    } else {
        return false;
    }
    if (endType == 0x2021) {
        r.range = QStringLiteral("endDate");
        r.endDate = minutesToDate(endDate);
    } else if (endType == 0x2022) {
        r.range = QStringLiteral("numbered");
        r.count = int(qMin<quint32>(count, 999));
    }
    if (!readExceptions(c, modifiedDates.size(), codePage, &r.overrides)) return false;
    // DeletedInstanceDates holds ORIGINAL dates; ModifiedInstanceDates holds
    // the NEW dates. Subtract originals decoded from ExceptionInfo, not the
    // modified-date list, or moving an occurrence deletes its replacement.
    QList<QDate> replaced;
    for (const auto &entry:r.overrides) replaced.append(QDate::fromString(entry.toMap().value(QStringLiteral("recurrenceId")).toString().left(10),Qt::ISODate));
    for (const QDate &date : std::as_const(deletedDates)) {
        if (!replaced.contains(date)) r.deletedDates.append(date);
    }
    *out = r;
    return true;
}


} // namespace QindaOffice::Mail::OutlookDetail
