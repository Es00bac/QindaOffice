// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (M76): the standard Outlook data file of tst_mail_pst and
// tst_qindamail_pst, built by pst_builder.h in either format and encoding.
//
//   Top of Personal Folders
//     Inbox            Quarterly report (HTML + inline picture + PDF, To/Cc/Bcc),
//                      Plain hello (unread, flagged), Rich memo (RTF only),
//                      Fwd: Original (an embedded message)
//       Projects       Project kickoff (a 40 KB body: a multi-block subnode)
//     Sent Items       Re: Quarterly report
//     Contacts         Grace Hopper (IPM.Contact, picture), Team (IPM.DistList:
//                      Grace by entry id + Alan Turing as a one-off address)
//     Calendar         Design review (weekly Mon/Wed ×10, one deleted, Pacific
//                      zone, attendees), Holiday (all day)
//     Tasks            Send brand book (in progress, high), Archive invoices (done)
//     Notes            Buy milk (a sticky note: not imported)
//     Deleted Items    (empty)

#include "cfb_writer_msg.h"
#include "pst_builder.h"

namespace QindaOffice::Mail::Test {

inline QDateTime pstUtc(int y, int mo, int d, int h = 0, int mi = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone(QTimeZone::UTC));
}

inline quint32 pstMinutes(const QDate &date)
{
    return quint32(QDate(1601, 1, 1).daysTo(date) * 1440);
}

inline QByteArray pstOneOff(const QString &name, const QString &address)
{
    using namespace PstBuild;
    return le32(0) + QByteArray::fromHex("812b1fa4bea310199d6e00dd010f5402") + le16(0) + le16(0x8001)
        + utf16(name) + le16(0) + utf16(QStringLiteral("SMTP")) + le16(0) + utf16(address) + le16(0);
}

inline QByteArray pstWrappedContact(const QByteArray &recordKey, quint32 nid)
{
    using namespace PstBuild;
    return le32(0) + QByteArray::fromHex("c091add3519dcf11a4a900aa0047faa4") + char(0xC3) + le32(0) + recordKey + le32(nid);
}

// MS-OXOCAL 2.2.1.44.5 AppointmentRecurrencePattern: weekly Monday and
// Wednesday, ten occurrences from 5 October 2026, 12 October deleted.
inline QByteArray pstWeeklyRecurrence()
{
    using namespace PstBuild;
    QByteArray b = le16(0x3004) + le16(0x3004) + le16(0x200B) + le16(0x0001) + le16(0);
    b += le32(0) + le32(1) + le32(0) + le32(0x02 | 0x08);
    b += le32(0x2022) + le32(10) + le32(0);
    b += le32(1) + le32(pstMinutes(QDate(2026, 10, 12)));
    b += le32(0);
    b += le32(pstMinutes(QDate(2026, 10, 5))) + le32(pstMinutes(QDate(2026, 11, 4)));
    b += le32(0x3006) + le32(0x3009) + le32(540) + le32(600) + le16(0) + le32(0) + le32(0);
    return b;
}

inline QByteArray pstZoneDefinition(const QString &key)
{
    using namespace PstBuild;
    const QByteArray name = utf16(key);
    return bytes({0x02, 0x01}) + le16(quint16(6 + name.size())) + le16(0x0002)
        + le16(quint16(key.size())) + name + le16(0);
}

inline QByteArray pstPicture()
{
    return QByteArray("\x89PNG\r\n\x1A\n", 8) + QByteArray(64, 'p');
}

inline PstMessageSpec pstMail(PstBuilder &b, const QString &subject, const QString &fromName, const QString &from,
                              const QDateTime &date, bool read)
{
    PstMessageSpec m;
    const bool u = b.unicode();
    m.props.text(0x001A, QStringLiteral("IPM.Note"), u).text(0x0037, subject, u).time(0x0039, date)
        .time(0x0E06, date.addSecs(60)).longValue(0x0E07, read ? 1 : 0).text(0x0042, fromName, u)
        .text(0x5D02, from, u).text(0x0064, QStringLiteral("SMTP"), u).text(0x0065, from, u);
    return m;
}

inline PropList pstRecipient(PstBuilder &b, quint32 type, const QString &name, const QString &addressType,
                             const QString &email, const QString &smtp = {}, quint32 flags = 0)
{
    PropList r;
    const bool u = b.unicode();
    r.longValue(0x0C15, type).text(0x3001, name, u).text(0x3002, addressType, u).text(0x3003, email, u);
    if (!smtp.isEmpty()) r.text(0x39FE, smtp, u);
    if (flags) r.longValue(0x5FFD, flags);
    return r;
}

inline PstFolderSpec pstMailFolders(PstBuilder &b)
{
    const bool u = b.unicode();
    PstMessageSpec report = pstMail(b, QStringLiteral("Quarterly report"), QStringLiteral("Ada Lovelace"),
                                    QStringLiteral("ada@example.org"), pstUtc(2026, 9, 30, 14), true);
    report.props.text(0x1035, QStringLiteral("<q3@example.org>"), u).text(0x1000, QStringLiteral("Numbers up"), u)
        .longValue(0x3FDE, 65001)
        .binary(0x1013, QByteArray("<html><body><p>Numbers <b>up</b> <img src=\"cid:chart@example.org\"></p></body></html>"));
    report.recipients = {pstRecipient(b, 1, QStringLiteral("Bob Example"), QStringLiteral("SMTP"), QStringLiteral("bob@example.org")),
                         pstRecipient(b, 2, QStringLiteral("Cy Young"), QStringLiteral("EX"),
                                      QStringLiteral("/O=EXAMPLE/OU=EXCHANGE/CN=RECIPIENTS/CN=CY"), QStringLiteral("cy@example.org")),
                         pstRecipient(b, 3, QStringLiteral("Dee"), QStringLiteral("SMTP"), QStringLiteral("dee@example.org"))};
    PstAttachmentSpec pdf;
    QByteArray pdfBytes("%PDF-1.4\n");
    pdfBytes += QByteArray(6000, 'x');
    pdf.props.longValue(0x3705, 1).text(0x3707, QStringLiteral("report.pdf"), u).text(0x370E, QStringLiteral("application/pdf"), u)
        .binary(0x3701, pdfBytes);
    PstAttachmentSpec chart;
    chart.props.longValue(0x3705, 1).text(0x3707, QStringLiteral("chart.png"), u).text(0x370E, QStringLiteral("image/png"), u)
        .text(0x3712, QStringLiteral("chart@example.org"), u).boolValue(0x7FFE, true).binary(0x3701, pstPicture());
    report.attachments = {pdf, chart};

    PstMessageSpec hello = pstMail(b, QStringLiteral("Plain hello"), QStringLiteral("Bob Example"),
                                   QStringLiteral("bob@example.org"), pstUtc(2026, 10, 1, 9), false);
    hello.props.text(0x1000, QStringLiteral("Hello there,\nplain text only."), u).longValue(0x1090, 2);
    hello.recipients = {pstRecipient(b, 1, QStringLiteral("Ada Lovelace"), QStringLiteral("SMTP"), QStringLiteral("ada@example.org"))};

    PstMessageSpec memo = pstMail(b, QStringLiteral("Rich memo"), QStringLiteral("Cy Young"), QStringLiteral("cy@example.org"),
                                  pstUtc(2026, 10, 2, 11), true);
    memo.props.binary(0x1009, compressRtfLzfu("{\\rtf1\\ansi\\fromtext Hello from RTF\\par}"));

    auto original = std::make_shared<PstMessageSpec>(pstMail(b, QStringLiteral("Original"), QStringLiteral("Eve"),
                                                             QStringLiteral("eve@example.org"), pstUtc(2026, 9, 1, 8), true));
    original->props.text(0x1000, QStringLiteral("The original text"), u);
    original->recipients = {pstRecipient(b, 1, QStringLiteral("Ada Lovelace"), QStringLiteral("SMTP"), QStringLiteral("ada@example.org"))};
    PstMessageSpec forward = pstMail(b, QStringLiteral("Fwd: Original"), QStringLiteral("Ada Lovelace"),
                                     QStringLiteral("ada@example.org"), pstUtc(2026, 10, 3, 10), true);
    forward.props.text(0x1000, QStringLiteral("See below"), u);
    PstAttachmentSpec attached;
    attached.props.longValue(0x3705, 5).text(0x3001, QStringLiteral("Original"), u);
    attached.embedded = original;
    forward.attachments = {attached};

    PstMessageSpec kickoff = pstMail(b, QStringLiteral("Project kickoff"), QStringLiteral("Ada Lovelace"),
                                     QStringLiteral("ada@example.org"), pstUtc(2026, 10, 4, 12), true);
    QString longBody;
    for (int i = 0; i < 800; ++i) longBody += QStringLiteral("Line %1 of the kickoff notes.\n").arg(i, 4, 10, QLatin1Char('0'));
    kickoff.props.text(0x1000, longBody, u);

    PstMessageSpec reply = pstMail(b, QStringLiteral("Re: Quarterly report"), QStringLiteral("Ada Lovelace"),
                                   QStringLiteral("ada@example.org"), pstUtc(2026, 9, 30, 16), true);
    reply.props.text(0x1000, QStringLiteral("Thanks!"), u);
    reply.recipients = {pstRecipient(b, 1, QStringLiteral("Bob Example"), QStringLiteral("SMTP"), QStringLiteral("bob@example.org"))};

    PstFolderSpec inbox{QStringLiteral("Inbox"), QStringLiteral("IPF.Note"), {report, hello, memo, forward}, {}};
    inbox.children.append({QStringLiteral("Projects"), QStringLiteral("IPF.Note"), {kickoff}, {}});
    PstFolderSpec top{b.top.name, {}, {}, {inbox, {QStringLiteral("Sent Items"), QStringLiteral("IPF.Note"), {reply}, {}}}};
    return top;
}

// The whole fixture. `contactNid` is the Grace Hopper item's id, which the
// group refers to (two passes: the first build assigns it).
inline QByteArray buildStandardPst(bool unicode, quint8 encoding, PstBuild::PstLayout *layout = nullptr,
                                   PstBuilder *out = nullptr)
{
    PstBuilder b(unicode, encoding);
    const bool u = unicode;
    const auto address = [&b](quint32 lid) { return b.named(Mapi::kPsetidAddress, lid); };
    const auto appointment = [&b](quint32 lid) { return b.named(Mapi::kPsetidAppointment, lid); };
    const auto task = [&b](quint32 lid) { return b.named(Mapi::kPsetidTask, lid); };
    const quint16 keywords = b.named(Mapi::kPsPublicStrings, QStringLiteral("Keywords"));

    b.top = pstMailFolders(b);
    PstMessageSpec grace;
    grace.props.text(0x001A, QStringLiteral("IPM.Contact"), u).text(0x0037, QStringLiteral("Grace Hopper"), u)
        .text(0x3001, QStringLiteral("Grace Hopper"), u).text(0x3A06, QStringLiteral("Grace"), u)
        .text(0x3A11, QStringLiteral("Hopper"), u).text(0x3A16, QStringLiteral("Navy"), u)
        .text(0x3A17, QStringLiteral("Rear Admiral"), u).text(0x3A18, QStringLiteral("Computing"), u)
        .text(address(0x8083), QStringLiteral("grace@example.org"), u).text(address(0x8082), QStringLiteral("SMTP"), u)
        .text(address(0x8093), QStringLiteral("/O=NAVY/OU=EXCH/CN=RECIPIENTS/CN=GHOPPER"), u)
        .text(address(0x8092), QStringLiteral("EX"), u).text(address(0x8094), QStringLiteral("grace.hopper@navy.example"), u)
        .text(0x3A08, QStringLiteral("+1 555 0100"), u).text(0x3A1C, QStringLiteral("+1 555 0101"), u)
        .text(0x3A5D, QStringLiteral("1 Bug Lane"), u).text(0x3A59, QStringLiteral("Arlington"), u)
        .text(0x3A5C, QStringLiteral("VA"), u).text(0x3A5B, QStringLiteral("22201"), u).text(0x3A5A, QStringLiteral("USA"), u)
        .text(address(0x8045), QStringLiteral("2 Navy Way"), u).text(address(0x8046), QStringLiteral("Washington"), u)
        .time(address(0x80DE), pstUtc(1906, 12, 9)).time(0x3A42, pstUtc(1906, 12, 9, 5))
        .text(0x3A50, QStringLiteral("https://grace.example.org"), u).text(0x1000, QStringLiteral("Pioneer of compilers."), u)
        .mvUnicode(keywords, {QStringLiteral("Navy"), QStringLiteral("Computing")});
    PstAttachmentSpec photo;
    photo.props.longValue(0x3705, 1).text(0x3707, QStringLiteral("ContactPicture.jpg"), u).boolValue(0x7FFF, true)
        .binary(0x3701, pstPicture());
    grace.attachments = {photo};
    PstMessageSpec team;
    team.props.text(0x001A, QStringLiteral("IPM.DistList"), u).text(0x0037, QStringLiteral("Team"), u)
        .text(0x3001, QStringLiteral("Team"), u).text(address(0x8053), QStringLiteral("Team"), u)
        .text(0x1000, QStringLiteral("Everyone on the project"), u);

    PstMessageSpec review;
    review.props.text(0x001A, QStringLiteral("IPM.Appointment"), u).text(0x0037, QStringLiteral("Design review"), u)
        .text(0x1000, QStringLiteral("Agenda: wireframes"), u).text(appointment(0x8208), QStringLiteral("Room 4"), u)
        .time(appointment(0x820D), pstUtc(2026, 10, 5, 16)).time(appointment(0x820E), pstUtc(2026, 10, 5, 17))
        .boolValue(appointment(0x8215), false).boolValue(appointment(0x8223), true)
        .binary(appointment(0x8216), pstWeeklyRecurrence())
        .binary(appointment(0x825E), pstZoneDefinition(QStringLiteral("Pacific Standard Time")))
        .binary(b.named(Mapi::kPsetidMeeting, 0x0003), QByteArray::fromHex("040000008200e00074c5b7101a82e008abcdef"))
        .text(0x0042, QStringLiteral("Ada Lovelace"), u).text(0x5D02, QStringLiteral("ada@example.org"), u);
    review.recipients = {pstRecipient(b, 1, QStringLiteral("Ada Lovelace"), QStringLiteral("SMTP"), QStringLiteral("ada@example.org"), {}, 0x0003),
                         pstRecipient(b, 1, QStringLiteral("Bob Example"), QStringLiteral("SMTP"), QStringLiteral("bob@example.org"))};
    PstMessageSpec holiday;
    holiday.props.text(0x001A, QStringLiteral("IPM.Appointment"), u).text(0x0037, QStringLiteral("Holiday"), u)
        .time(appointment(0x820D), pstUtc(2026, 12, 25, 8)).time(appointment(0x820E), pstUtc(2026, 12, 26, 8))
        .boolValue(appointment(0x8215), true)
        .binary(appointment(0x825E), pstZoneDefinition(QStringLiteral("Pacific Standard Time")));

    PstMessageSpec brandBook;
    brandBook.props.text(0x001A, QStringLiteral("IPM.Task"), u).text(0x0037, QStringLiteral("Send brand book"), u)
        .text(0x1000, QStringLiteral("Final PDF to Northwind"), u).longValue(task(0x8101), 1).doubleValue(task(0x8102), 0.5)
        .time(task(0x8104), pstUtc(2026, 10, 1)).time(task(0x8105), pstUtc(2026, 10, 15)).boolValue(task(0x811C), false)
        .longValue(0x0017, 2);
    PstMessageSpec invoices;
    invoices.props.text(0x001A, QStringLiteral("IPM.Task"), u).text(0x0037, QStringLiteral("Archive invoices"), u)
        .longValue(task(0x8101), 2).doubleValue(task(0x8102), 1.0).boolValue(task(0x811C), true)
        .time(task(0x810F), pstUtc(2026, 9, 20)).longValue(0x0017, 0);
    PstMessageSpec sticky;
    sticky.props.text(0x001A, QStringLiteral("IPM.StickyNote"), u).text(0x0037, QStringLiteral("Buy milk"), u);

    b.top.children.append({QStringLiteral("Contacts"), QStringLiteral("IPF.Contact"), {grace, team}, {}});
    b.top.children.append({QStringLiteral("Calendar"), QStringLiteral("IPF.Appointment"), {review, holiday}, {}});
    b.top.children.append({QStringLiteral("Tasks"), QStringLiteral("IPF.Task"), {brandBook, invoices}, {}});
    b.top.children.append({QStringLiteral("Notes"), QStringLiteral("IPF.StickyNote"), {sticky}, {}});
    b.top.children.append({QStringLiteral("Deleted Items"), QStringLiteral("IPF.Note"), {}, {}});
    b.build();   // first pass: learn Grace's node id (ids are assigned in folder order)
    const quint32 graceNid = b.messageNids.at(6);
    b.top.children[2].messages[1].props.mvBinary(address(0x8055), {pstWrappedContact(b.recordKey, graceNid),
                                                                   pstOneOff(QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org"))})
        .mvBinary(address(0x8054), {pstOneOff(QStringLiteral("Grace Hopper"), QStringLiteral("grace@example.org")),
                                    pstOneOff(QStringLiteral("Alan Turing"), QStringLiteral("alan@example.org"))});
    const QByteArray file = b.build(layout);
    if (out) *out = b;
    return file;
}

} // namespace QindaOffice::Mail::Test
