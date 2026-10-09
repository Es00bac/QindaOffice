// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_pst (M76): crypto primitives, formats and the folder tree. See
// tst_mail_pst.h.

#include "tst_mail_pst.h"

#include "outlook_items.h"
#include "pst_crypt.h"
#include "pst_fixture.h"

#include <QRandomGenerator>
#include <QtTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

void TestMailPst::cryptTables()
{
    // MS-PST 5.1 prints mpbbCrypt; its first row is mpbbR.
    const quint8 printed[] = {65, 54, 19, 98, 168, 33, 110, 187, 244, 22, 204, 4, 127, 100, 232, 93};
    for (int i = 0; i < 16; ++i) QCOMPARE(int(Pst::encodeTable()[i]), int(printed[i]));
    QSet<int> seen;
    for (int i = 0; i < 256; ++i) {
        seen.insert(Pst::decodeTable()[i]);
        QCOMPARE(int(Pst::decodeTable()[Pst::encodeTable()[i]]), i);   // mpbbI = mpbbR⁻¹
        QCOMPARE(int(Pst::middleTable()[Pst::middleTable()[i]]), i);   // mpbbS is an involution
    }
    QCOMPARE(seen.size(), 256);
    QRandomGenerator rng(0x7057);
    QByteArray data(9000, '\0');
    for (char &c : data) c = char(rng.bounded(256));
    QByteArray copy = data;
    Pst::permute(copy.data(), copy.size(), true);
    QVERIFY(copy != data);
    Pst::permute(copy.data(), copy.size(), false);
    QCOMPARE(copy, data);
    for (const quint32 key : {0u, 1u, 0x10024u, 0xDEADBEEFu}) {
        copy = data;
        Pst::cyclic(copy.data(), copy.size(), key);
        QVERIFY(copy != data);
        Pst::cyclic(copy.data(), copy.size(), key);   // its own inverse
        QCOMPARE(copy, data);
    }
}

void TestMailPst::checksumsAndSignatures()
{
    // CRC-32 without pre/post inversion (zlib: ~crc32(data, 0xFFFFFFFF)).
    QCOMPARE(Pst::crc32(QByteArray("123456789")), 0x2DFD2D88u);
    QCOMPARE(Pst::crc32(QByteArray()), 0u);
    QCOMPARE(Pst::crc32(QByteArray("The quick brown fox jumps over the lazy dog")), 0xB9C60808u);
    QCOMPARE(Pst::signature(0x4400, 0x10), quint16(0x4410));
    QCOMPARE(Pst::signature(0x12345678, 0x1), quint16(0x1234 ^ 0x5679));
}

void TestMailPst::formats_data()
{
    QTest::addColumn<bool>("unicode");
    QTest::addColumn<int>("encoding");
    for (const bool unicode : {true, false}) {
        for (const int encoding : {0, 1, 2}) {
            QTest::addRow("%s-%s", unicode ? "unicode" : "ansi", encoding == 0 ? "none" : encoding == 1 ? "permute" : "cyclic")
                << unicode << encoding;
        }
    }
}

void TestMailPst::formats()
{
    QFETCH(bool, unicode);
    QFETCH(int, encoding);
    PstBuilder builder(unicode);
    const QByteArray bytes = buildStandardPst(unicode, quint8(encoding), nullptr, &builder);
    OpenedPst pst;
    QString error;
    QVERIFY2(pst.open(bytes, &error), qPrintable(error));
    QCOMPARE(pst.file.database().format(), unicode ? Pst::Format::Unicode : Pst::Format::Ansi);
    QCOMPARE(int(pst.file.database().encoding()), encoding);
    QVERIFY(!pst.file.database().isOst());
    QCOMPARE(pst.file.displayName(), QStringLiteral("Personal Folders"));
    QCOMPARE(pst.file.recordKey(), builder.recordKey);
    QCOMPARE(pst.file.ipmSubtree(), builder.topNid);
    QVERIFY(pst.file.namedProperties().size() > 20);
    // Every item of every folder opens and maps.
    int items = 0;
    QList<quint32> pending{pst.file.ipmSubtree()};
    while (!pending.isEmpty()) {
        Pst::Folder folder;
        QVERIFY2(pst.file.readFolder(pending.takeFirst(), &folder, &error), qPrintable(error));
        pending += folder.subfolders;
        for (const quint32 nid : std::as_const(folder.items)) {
            std::unique_ptr<Mapi::Message> message = pst.file.openMessage(nid, &error);
            QVERIFY2(message, qPrintable(error));
            OutlookItem item;
            QVERIFY2(readOutlookItem(*message, &item, &error), qPrintable(error));
            ++items;
        }
    }
    QCOMPARE(items, 13);
    QCOMPARE(builder.messageNids.size(), 13);
}

void TestMailPst::folderTree()
{
    PstBuilder builder(true);
    const QByteArray bytes = buildStandardPst(true, 1, nullptr, &builder);
    OpenedPst pst;
    QString error;
    QVERIFY2(pst.open(bytes, &error), qPrintable(error));
    Pst::Folder top;
    QVERIFY2(pst.file.readFolder(pst.file.ipmSubtree(), &top, &error), qPrintable(error));
    QCOMPARE(top.name, QStringLiteral("Top of Personal Folders"));
    QStringList names, classes;
    QList<int> counts;
    for (const quint32 nid : std::as_const(top.subfolders)) {
        Pst::Folder child;
        QVERIFY2(pst.file.readFolder(nid, &child, &error), qPrintable(error));
        names.append(child.name);
        classes.append(child.containerClass);
        counts.append(int(child.items.size()));
        if (child.name == QLatin1String("Inbox")) {
            QCOMPARE(child.subfolders.size(), qsizetype(1));
            Pst::Folder projects;
            QVERIFY(pst.file.readFolder(child.subfolders.first(), &projects, &error));
            QCOMPARE(projects.name, QStringLiteral("Projects"));
            QCOMPARE(projects.items.size(), qsizetype(1));
        }
    }
    QCOMPARE(names, (QStringList{QStringLiteral("Inbox"), QStringLiteral("Sent Items"), QStringLiteral("Contacts"),
                                 QStringLiteral("Calendar"), QStringLiteral("Tasks"), QStringLiteral("Notes"),
                                 QStringLiteral("Deleted Items")}));
    QCOMPARE(classes.at(2), QStringLiteral("IPF.Contact"));
    QCOMPARE(classes.at(3), QStringLiteral("IPF.Appointment"));
    QCOMPARE(counts, (QList<int>{4, 1, 2, 2, 2, 1, 0}));
    // The root folder lists the IPM subtree; a missing node is an error.
    Pst::Folder root;
    QVERIFY(pst.file.readFolder(0x122, &root, &error));
    QCOMPARE(root.subfolders, QList<quint32>{builder.topNid});
    QVERIFY(!pst.file.readFolder(0x7FFFE2, &root, &error));
    QVERIFY(error.contains(QLatin1String("missing")));
    QVERIFY(!pst.file.openMessage(0x7FFFE4, &error));
}

void TestMailPst::ostSignature()
{
    // An offline cache in the same format reads like a .pst; Outlook 2013's
    // 4 KiB-page .ost (wVer 36) is refused with a sentence.
    QByteArray bytes = buildStandardPst(true, 1);
    bytes[8] = 'S';
    bytes[9] = 'O';
    PstBuild::put32(bytes, 4, Pst::crc32(bytes.constData() + 8, 471));
    PstBuild::put32(bytes, 524, Pst::crc32(bytes.constData() + 8, 516));
    OpenedPst ost;
    QString error;
    QVERIFY2(ost.open(bytes, &error), qPrintable(error));
    QVERIFY(ost.file.database().isOst());
    PstBuild::put16(bytes, 10, 36);
    PstBuild::put32(bytes, 4, Pst::crc32(bytes.constData() + 8, 471));
    PstBuild::put32(bytes, 524, Pst::crc32(bytes.constData() + 8, 516));
    OpenedPst modern;
    QVERIFY(!modern.open(bytes, &error));
    QVERIFY2(error.contains(QLatin1String("Outlook 2013")), qPrintable(error));
}

void TestMailPst::refusesOtherFiles()
{
    QString error;
    OpenedPst empty;
    QVERIFY(!empty.open(QByteArray(), &error));
    QVERIFY(error.contains(QLatin1String("too small")));
    OpenedPst text;
    QVERIFY(!text.open(QByteArray(1024, 'a'), &error));
    QVERIFY(error.contains(QLatin1String("bad signature")));
    QByteArray bytes = buildStandardPst(false, 0);
    bytes[20] = 'x';   // inside the CRC'd header
    OpenedPst damaged;
    QVERIFY(!damaged.open(bytes, &error));
    QVERIFY(error.contains(QLatin1String("checksum")));
    bytes = buildStandardPst(true, 0);
    bytes[513] = char(0x10);   // Windows Information Protection
    PstBuild::put32(bytes, 4, Pst::crc32(bytes.constData() + 8, 471));
    PstBuild::put32(bytes, 524, Pst::crc32(bytes.constData() + 8, 516));
    OpenedPst protectedFile;
    QVERIFY(!protectedFile.open(bytes, &error));
    QVERIFY(error.contains(QLatin1String("Information Protection")));
}

QTEST_GUILESS_MAIN(TestMailPst)
