// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_pst (M76): data and subnode trees of every depth, the corrupt
// variants failing cleanly, and a 10 000-message file in time. See
// tst_mail_pst.h.

#include "tst_mail_pst.h"

#include "mapi_mail.h"
#include "outlook_items.h"
#include "pst_crypt.h"
#include "pst_fixture.h"
#include "rfc822_writer.h"

#include <QElapsedTimer>
#include <QtEndian>
#include <QRandomGenerator>
#include <QtTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

// Opens and walks a whole file; returns "" when everything read, else the
// first error — and "(…)" when the reader broke its contract.
QString walkAll(const QByteArray &bytes)
{
    OpenedPst pst;
    QString error;
    if (!pst.open(bytes, &error)) return error.isEmpty() ? QStringLiteral("(silent open failure)") : error;
    QList<quint32> pending{pst.file.ipmSubtree()};
    QSet<quint32> seen;
    QString first;
    while (!pending.isEmpty()) {
        const quint32 nid = pending.takeFirst();
        if (seen.contains(nid) || seen.size() > 1000) continue;
        seen.insert(nid);
        Pst::Folder folder;
        if (!pst.file.readFolder(nid, &folder, &error)) {
            if (first.isEmpty()) first = error.isEmpty() ? QStringLiteral("(silent folder failure)") : error;
            continue;
        }
        pending += folder.subfolders;
        for (const quint32 item : std::as_const(folder.items)) {
            std::unique_ptr<Mapi::Message> message = pst.file.openMessage(item, &error);
            OutlookItem out;
            if (!message || !readOutlookItem(*message, &out, &error)) {
                if (first.isEmpty()) first = error.isEmpty() ? QStringLiteral("(silent item failure)") : error;
            }
        }
    }
    return first;
}

int leafEntrySize(const PstBuild::PstLayout &layout, bool nbt)
{
    return layout.unicode ? (nbt ? 32 : 24) : (nbt ? 16 : 12);
}

// Re-seals a B-tree page after an edit (CRC over the bytes before the trailer).
void resealPage(QByteArray &bytes, const PstBuild::PstLayout &layout, qint64 ib)
{
    const qint64 data = 512 - layout.pageTrailer();
    const quint32 crc = Pst::crc32(bytes.constData() + ib, data);
    PstBuild::put32(bytes, ib + data + (layout.unicode ? 4 : 8), crc);
}

PstMessageSpec smallMail(PstBuilder &b, int i)
{
    PstMessageSpec m = pstMail(b, QStringLiteral("Message %1").arg(i), QStringLiteral("Sender %1").arg(i % 97),
                               QStringLiteral("sender%1@example.org").arg(i % 97), pstUtc(2026, 1, 1).addSecs(i * 60), i % 3 != 0);
    m.props.text(0x1000, QStringLiteral("Body of message %1. ").arg(i).repeated(6), b.unicode());
    m.recipients = {pstRecipient(b, 1, QStringLiteral("Ada"), QStringLiteral("SMTP"), QStringLiteral("ada@example.org"))};
    return m;
}

} // namespace

void TestMailPst::deepTrees()
{
    // 2 000 items in a folder (a table on a many-block heap with a bitmap
    // header and an index level, its rows in a subnode), and one message with
    // 400 attachments (an SIBLOCK) one of which is 9 MB (an XXBLOCK).
    PstBuilder b(true, 1);
    PstFolderSpec bulk{QStringLiteral("Bulk"), QStringLiteral("IPF.Note"), {}, {}};
    for (int i = 0; i < 2000; ++i) bulk.messages.append(smallMail(b, i));
    PstMessageSpec big = smallMail(b, 9999);
    QByteArray huge(9 * 1024 * 1024, '\0');
    for (qsizetype i = 0; i < huge.size(); ++i) huge[i] = char((i * 31) % 251);
    for (int i = 0; i < 400; ++i) {
        PstAttachmentSpec a;
        a.props.longValue(0x3705, 1).unicode(0x3707, QStringLiteral("part%1.bin").arg(i))
            .binary(0x3701, i == 399 ? huge : QByteArray(100 + i, char('a' + i % 26)));
        big.attachments.append(a);
    }
    b.top.children = {bulk, {QStringLiteral("Big"), QStringLiteral("IPF.Note"), {big}, {}}};
    PstBuild::PstLayout layout;
    OpenedPst pst;
    QString error;
    QVERIFY2(pst.open(b.build(&layout), &error), qPrintable(error));
    Pst::Folder folder;
    QVERIFY2(pst.file.readFolder(b.folderNids.value(QStringLiteral("Bulk")), &folder, &error), qPrintable(error));
    QCOMPARE(folder.items.size(), qsizetype(2000));
    QCOMPARE(folder.items.first(), b.messageNids.first());
    std::unique_ptr<Mapi::Message> message = pst.file.openMessage(folder.items.at(1234), &error);
    QVERIFY2(message, qPrintable(error));
    OutlookItem item;
    QVERIFY2(readOutlookItem(*message, &item, &error), qPrintable(error));
    QCOMPARE(item.mail.subject, QStringLiteral("Message 1234"));
    message = pst.file.openMessage(b.messageNids.last(), &error);
    QVERIFY2(message && readOutlookItem(*message, &item, &error), qPrintable(error));
    QCOMPARE(item.mail.attachments.size(), qsizetype(400));
    QCOMPARE(item.mail.attachments.at(7).name, QStringLiteral("part7.bin"));
    QCOMPARE(item.mail.attachments.at(7).data, QByteArray(107, 'h'));
    QVERIFY(item.mail.attachments.at(399).data == huge);
}

void TestMailPst::truncatedFiles()
{
    const QByteArray bytes = buildStandardPst(true, 1);
    for (const qsizetype size : {qsizetype(0), qsizetype(100), qsizetype(563), qsizetype(600), bytes.size() / 2, bytes.size() - 1}) {
        const QString result = walkAll(bytes.left(size));
        QVERIFY2(!result.isEmpty() && !result.startsWith(u'('), qPrintable(result));
    }
    // A cut file whose header claims the cut size: it opens, and what is
    // gone fails item by item with an error.
    QByteArray cut = bytes.left(bytes.size() * 2 / 3);
    PstBuild::put64(cut, 184, quint64(cut.size()));
    PstBuild::put32(cut, 4, Pst::crc32(cut.constData() + 8, 471));
    PstBuild::put32(cut, 524, Pst::crc32(cut.constData() + 8, 516));
    const QString result = walkAll(cut);
    QVERIFY2(!result.isEmpty() && !result.startsWith(u'('), qPrintable(result));
}

void TestMailPst::cyclicIndex()
{
    PstBuild::PstLayout layout;
    QByteArray bytes = buildStandardPst(true, 1, &layout);
    int top = 0;
    for (const auto &page : std::as_const(layout.pages))
        if (page.nbt) top = qMax(top, page.level);
    QVERIFY2(top >= 1, "the fixture's node index must have more than one level");
    PstBuild::PstLayout::Page root;
    for (const auto &page : std::as_const(layout.pages))
        if (page.nbt && page.level == top) root = page;
    // The root's second entry now points back at the root itself.
    const qint64 entry = root.ib + 24;
    PstBuild::put64(bytes, entry + 8, root.bid);
    PstBuild::put64(bytes, entry + 16, quint64(root.ib));
    resealPage(bytes, layout, root.ib);
    const QString result = walkAll(bytes);
    QVERIFY2(result.contains(QLatin1String("loops back")), qPrintable(result));
}

void TestMailPst::badBlockSize()
{
    for (const bool tooLarge : {false, true}) {
        PstBuild::PstLayout layout;
        PstBuilder builder(true);
        QByteArray bytes = buildStandardPst(true, 1, &layout, &builder);
        // Find the block-index entry of the first item's data and change its size.
        OpenedPst probe;
        QString error;
        QVERIFY(probe.open(bytes, &error));
        const quint64 target = probe.file.database().findNode(builder.messageNids.first(), &error)->bidData;
        bool patched = false;
        for (const auto &page : std::as_const(layout.pages)) {
            if (page.nbt || page.level != 0) continue;
            const int count = quint8(bytes.at(page.ib + 488));
            for (int i = 0; i < count && !patched; ++i) {
                const qint64 at = page.ib + qint64(i) * leafEntrySize(layout, false);
                if (quint64(qFromLittleEndian<quint64>(bytes.constData() + at)) != target) continue;
                const quint16 cb = qFromLittleEndian<quint16>(bytes.constData() + at + 16);
                PstBuild::put16(bytes, at + 16, tooLarge ? 9000 : quint16(cb + 64));
                resealPage(bytes, layout, page.ib);
                patched = true;
            }
        }
        QVERIFY(patched);
        const QString result = walkAll(bytes);
        QVERIFY2(result.contains(tooLarge ? QLatin1String("bad size") : QLatin1String("does not match")), qPrintable(result));
    }
}

void TestMailPst::badChecksum()
{
    PstBuild::PstLayout layout;
    QByteArray bytes = buildStandardPst(false, 2, &layout);
    QByteArray damaged = bytes;
    const auto &block = layout.blocks.at(layout.blocks.size() / 2);
    damaged[block.ib + block.cb / 2] = char(damaged.at(block.ib + block.cb / 2) ^ 0x5A);
    QString result = walkAll(damaged);
    QVERIFY2(result.contains(QLatin1String("checksum mismatch")), qPrintable(result));
    damaged = bytes;
    const auto &page = layout.pages.first();
    damaged[page.ib + 3] = char(damaged.at(page.ib + 3) ^ 0x01);
    result = walkAll(damaged);
    QVERIFY2(result.contains(QLatin1String("checksum mismatch")), qPrintable(result));
}

void TestMailPst::corruptionFuzz()
{
    for (const bool unicode : {true, false}) {
        const QByteArray bytes = buildStandardPst(unicode, 1);
        QVERIFY(walkAll(bytes).isEmpty());
        QRandomGenerator rng(unicode ? 0x5157 : 0xA751);
        int failures = 0;
        for (int i = 0; i < 150; ++i) {
            QByteArray copy = bytes;
            // Past the header: a damaged header is refused before anything else.
            const int flips = 1 + rng.bounded(8);
            for (int k = 0; k < flips; ++k) copy[0x4400 + rng.bounded(int(copy.size() - 0x4400))] = char(rng.bounded(256));
            const QString result = walkAll(copy);
            QVERIFY2(!result.startsWith(u'('), qPrintable(result));
            if (!result.isEmpty()) ++failures;
        }
        QVERIFY2(failures > 60, qPrintable(QString::number(failures)));
    }
}

void TestMailPst::tenThousandMessages()
{
    // AGENT-NOTE: the bound is for a Debug build on the desktop (24 threads,
    // one used); docs/verification.md records the measured time.
    PstBuilder b(true, 1);
    PstFolderSpec archive{QStringLiteral("Archive"), QStringLiteral("IPF.Note"), {}, {}};
    for (int i = 0; i < 10000; ++i) archive.messages.append(smallMail(b, i));
    b.top.children = {archive};
    const QByteArray bytes = b.build();
    QElapsedTimer timer;
    timer.start();
    OpenedPst pst;
    QString error;
    QVERIFY2(pst.open(bytes, &error), qPrintable(error));
    Pst::Folder folder;
    QVERIFY2(pst.file.readFolder(b.folderNids.value(QStringLiteral("Archive")), &folder, &error), qPrintable(error));
    QCOMPARE(folder.items.size(), qsizetype(10000));
    qint64 written = 0;
    for (const quint32 nid : std::as_const(folder.items)) {
        std::unique_ptr<Mapi::Message> message = pst.file.openMessage(nid, &error);
        OutlookItem item;
        QVERIFY2(message && readOutlookItem(*message, &item, &error), qPrintable(error));
        written += writeRfc822(item.mail).size();
    }
    const qint64 elapsed = timer.elapsed();
    qInfo("10 000 messages (%lld KiB file) read and written as RFC 5322 in %lld ms", qlonglong(bytes.size() / 1024),
          qlonglong(elapsed));
    QVERIFY(written > 10000 * 300);
    QVERIFY2(elapsed < 20000, qPrintable(QString::number(elapsed)));
}
