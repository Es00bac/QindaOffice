// SPDX-License-Identifier: GPL-3.0-or-later
// tst_mail_msg, compound files: CfbReader over files built by cfb_writer.h
// (v3 and v4, mini and regular streams, DIFAT sectors) and over corrupted
// copies — every corruption must fail with a message, never crash.

#include "tst_mail_msg.h"

#include "cfb_reader.h"
#include "cfb_writer.h"

#include <QRandomGenerator>
#include <QTest>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

QByteArray pattern(qint64 size, int seed)
{
    QByteArray bytes(qsizetype(size), Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i) bytes[i] = char((i * 31 + seed * 7 + (i >> 9)) & 0xFF);
    return bytes;
}

CfbNode sampleTree()
{
    QList<CfbNode> children;
    int seed = 0;
    for (const qint64 size : {0, 1, 63, 64, 4095, 4096, 100000})
        children.append(CfbNode::stream(QStringLiteral("s%1").arg(size), pattern(size, ++seed)));
    children.append(CfbNode::folder(
        QStringLiteral("Storage One"),
        {CfbNode::stream(QStringLiteral("inner small"), pattern(100, 50)),
         CfbNode::stream(QStringLiteral("inner big"), pattern(9000, 51)),
         CfbNode::folder(QStringLiteral("deeper"), {CfbNode::stream(QStringLiteral("leaf"), pattern(5, 52))})}));
    for (int i = 0; i < 40; ++i)
        children.append(CfbNode::stream(QStringLiteral("many_%1").arg(i), pattern(i * 37 % 300, 100 + i)));
    return CfbNode::folder(QStringLiteral("Root Entry"), children);
}

CfbNode flatTree(const QList<QPair<QString, qint64>> &streams)
{
    QList<CfbNode> children;
    int seed = 0;
    for (const auto &stream : streams) children.append(CfbNode::stream(stream.first, pattern(stream.second, ++seed)));
    return CfbNode::folder(QStringLiteral("Root Entry"), children);
}

void verifyTree(const CfbReader &reader, int storage, const CfbNode &node)
{
    QCOMPARE(reader.children(storage).size(), node.children.size());
    for (const CfbNode &expected : node.children) {
        const int id = reader.child(storage, expected.name);
        QVERIFY2(id >= 0, qPrintable(expected.name));
        const CfbReader::Entry *entry = reader.entry(id);
        QVERIFY(entry);
        QCOMPARE(entry->name, expected.name);
        if (expected.storage) {
            QVERIFY(entry->type == CfbReader::Entry::Storage);
            verifyTree(reader, id, expected);
            if (QTest::currentTestFailed()) return;
            continue;
        }
        QVERIFY(entry->type == CfbReader::Entry::Stream);
        QCOMPARE(entry->size, qint64(expected.data.size()));
        QByteArray data;
        QString error;
        QVERIFY2(reader.read(id, &data, &error), qPrintable(error));
        QCOMPARE(data, expected.data);
    }
}

// Opens `file` and reads every stream reachable from the root: the first
// error, or an empty string when everything read. Results in parentheses
// mean the reader broke its contract (failure without a message, …).
QString readEverything(const QByteArray &file)
{
    CfbReader reader;
    QString error;
    if (!reader.open(file, &error)) return error.isEmpty() ? QStringLiteral("(open failed silently)") : error;
    QList<int> pending{reader.root()};
    while (!pending.isEmpty()) {
        const int storage = pending.takeLast();
        for (const int id : reader.children(storage)) {
            const CfbReader::Entry *entry = reader.entry(id);
            if (!entry) return QStringLiteral("(a child without an entry)");
            if (entry->type == CfbReader::Entry::Storage) {
                pending.append(id);
                continue;
            }
            QByteArray data = QByteArrayLiteral("stale");
            if (!reader.read(id, &data, &error)) {
                if (!data.isEmpty()) return QStringLiteral("(failed read left data behind)");
                return error.isEmpty() ? QStringLiteral("(read failed silently)") : error;
            }
            if (data.size() != entry->size) return QStringLiteral("(read returned the wrong size)");
        }
    }
    return {};
}

QString openError(const QByteArray &file)
{
    CfbReader reader;
    QString error;
    if (reader.open(file, &error)) return {};
    return error.isEmpty() ? QStringLiteral("(open failed silently)") : error;
}

QString readError(const QByteArray &file, const QString &stream)
{
    CfbReader reader;
    QString error;
    if (!reader.open(file, &error)) return QStringLiteral("(open failed: %1)").arg(error);
    QByteArray data;
    if (reader.read(reader.child(reader.root(), stream), &data, &error)) return {};
    return error.isEmpty() ? QStringLiteral("(read failed silently)") : error;
}

} // namespace

#define EXPECT_ERROR(expression, needle)                                                   \
    do {                                                                                   \
        const QString message_ = (expression);                                             \
        QVERIFY2(!message_.isEmpty() && !message_.startsWith(u'('), qPrintable(message_)); \
        QVERIFY2(message_.contains(QLatin1String(needle), Qt::CaseInsensitive), qPrintable(message_)); \
    } while (false)

void TestMailMsg::cfbRoundTrip_data()
{
    QTest::addColumn<int>("version");
    QTest::newRow("v3, 512-byte sectors") << 3;
    QTest::newRow("v4, 4096-byte sectors") << 4;
}

void TestMailMsg::cfbRoundTrip()
{
    QFETCH(int, version);
    const CfbNode tree = sampleTree();
    CfbLayout layout;
    const QByteArray file = buildCfb(tree, {version, 0}, &layout);
    QCOMPARE(layout.shift, version == 4 ? 12 : 9);
    QVERIFY(layout.dirSectors.size() > 1);
    CfbReader reader;
    QString error;
    QVERIFY2(reader.open(file, &error), qPrintable(error));
    QCOMPARE(reader.majorVersion(), version);
    QCOMPARE(reader.root(), 0);
    QVERIFY(reader.entry(0)->type == CfbReader::Entry::Root);
    verifyTree(reader, reader.root(), tree);
    // Children come back in directory order: shorter names first, then by name.
    const QList<int> kids = reader.children(reader.root());
    for (qsizetype i = 1; i < kids.size(); ++i)
        QVERIFY(!cfbNameLess(reader.entry(kids.at(i))->name, reader.entry(kids.at(i - 1))->name));
    QVERIFY(!reader.entry(-1));
    QVERIFY(!reader.entry(1000000));
    if (layout.items.size() % (layout.sectorSize() / 128) != 0) QVERIFY(!reader.entry(int(layout.items.size())));
    QVERIFY(reader.children(reader.child(reader.root(), QStringLiteral("s1"))).isEmpty());
    QByteArray data;
    QVERIFY(!reader.read(reader.root(), &data, &error));
    QVERIFY(!error.isEmpty());
    // A reader that failed to open is closed.
    QVERIFY(!reader.open(QByteArray(10, 'x'), &error));
    QVERIFY(!reader.isOpen());
    QCOMPARE(reader.root(), -1);
    QVERIFY(reader.children(0).isEmpty());
}

void TestMailMsg::cfbCaseInsensitiveLookup()
{
    const CfbNode tree = CfbNode::folder(
        QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("__substg1.0_0037001F"), "a"),
         CfbNode::stream(QStringLiteral("Mixed Case Name"), "b"),
         CfbNode::stream(QStringLiteral("Äöü straße"), "c"),
         CfbNode::folder(QStringLiteral("Folder"), {CfbNode::stream(QStringLiteral("Inner"), "d")})});
    CfbReader reader;
    QString error;
    QVERIFY2(reader.open(buildCfb(tree), &error), qPrintable(error));
    const int root = reader.root();
    const int substg = reader.child(root, QStringLiteral("__substg1.0_0037001F"));
    QVERIFY(substg > 0);
    QCOMPARE(reader.child(root, QStringLiteral("__SUBSTG1.0_0037001f")), substg);
    QCOMPARE(reader.child(root, QStringLiteral("mixed case NAME")), reader.child(root, QStringLiteral("Mixed Case Name")));
    QVERIFY(reader.child(root, QStringLiteral("äÖÜ STRAßE")) > 0);
    QCOMPARE(reader.child(root, QStringLiteral("Mixed Case Nam")), -1);
    QCOMPARE(reader.child(root, QStringLiteral("missing")), -1);
    const int folder = reader.child(root, QStringLiteral("FOLDER"));
    QVERIFY(folder > 0);
    QVERIFY(reader.child(folder, QStringLiteral("inner")) > 0);
    QCOMPARE(reader.child(root, QStringLiteral("Inner")), -1);   // not recursive
    QCOMPARE(reader.child(-3, QStringLiteral("Inner")), -1);
    QCOMPARE(reader.child(substg, QStringLiteral("x")), -1);
}

void TestMailMsg::cfbRejectsBadHeaders()
{
    CfbLayout layout;
    const QByteArray good = buildCfb(flatTree({{QStringLiteral("a"), 10}, {QStringLiteral("b"), 5000}}), {}, &layout);
    QVERIFY(openError(good).isEmpty());
    EXPECT_ERROR(openError({}), "truncated");
    EXPECT_ERROR(openError(good.left(100)), "truncated");
    QByteArray bad = good;
    bad[0] = 'X';
    EXPECT_ERROR(openError(bad), "signature");
    bad = good;
    cfbPut16(bad, 0x1E, 12);   // version 3 with 4096-byte sectors
    EXPECT_ERROR(openError(bad), "version 3");
    bad = good;
    cfbPut16(bad, 0x1A, 5);
    EXPECT_ERROR(openError(bad), "version 5");
    bad = good;
    cfbPut16(bad, 0x1C, 0xFEFF);
    EXPECT_ERROR(openError(bad), "byte-order");
    bad = good;
    cfbPut16(bad, 0x20, 7);
    EXPECT_ERROR(openError(bad), "mini sector");
    bad = good;
    cfbPut32(bad, 0x38, 512);
    EXPECT_ERROR(openError(bad), "cutoff");
    bad = good;
    cfbPut32(bad, 0x2C, 100000);
    EXPECT_ERROR(openError(bad), "FAT claims");
    bad = good;
    cfbPut32(bad, 0x30, 0x00FFFFFF);
    EXPECT_ERROR(openError(bad), "out of range");
    bad = good;
    cfbPut32(bad, 0x30, kCfbEndOfChain);
    EXPECT_ERROR(openError(bad), "no directory");
    bad = good;
    bad[layout.dirEntryOffset(0) + 66] = 1;   // the root entry is a storage
    EXPECT_ERROR(openError(bad), "no root entry");
    const QByteArray v4 = buildCfb(flatTree({{QStringLiteral("a"), 10}}), {4, 0});
    QVERIFY(openError(v4).isEmpty());
    EXPECT_ERROR(openError(v4.left(2000)), "truncated");
    bad = v4;
    cfbPut16(bad, 0x1E, 9);   // version 4 with 512-byte sectors
    EXPECT_ERROR(openError(bad), "version 4");
}

void TestMailMsg::cfbTruncatedSector()
{
    const QByteArray big = pattern(100000, 9);   // 195 sectors + 160 bytes
    const QByteArray file = buildCfb(CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("small"), pattern(10, 1)), CfbNode::stream(QStringLiteral("big"), big)}));
    QCOMPARE(file.size() % 512, qsizetype(0));
    // The big stream is last; cut inside the 160 bytes it still needs.
    EXPECT_ERROR(readError(file.left(file.size() - 512 + 100), QStringLiteral("big")), "truncated");
    // Only the unused tail of its last sector is missing: still readable.
    CfbReader reader;
    QString error;
    QByteArray data;
    QVERIFY2(reader.open(file.left(file.size() - 512 + 160), &error), qPrintable(error));
    QVERIFY2(reader.read(reader.child(0, QStringLiteral("big")), &data, &error), qPrintable(error));
    QCOMPARE(data, big);
    // Whole sectors missing: the chain leaves the file.
    QVERIFY2(reader.open(file.left(file.size() - 3 * 512), &error), qPrintable(error));
    QVERIFY(!reader.read(reader.child(0, QStringLiteral("big")), &data, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(data.isEmpty());
    QVERIFY2(reader.read(reader.child(0, QStringLiteral("small")), &data, &error), qPrintable(error));
    QCOMPARE(data, pattern(10, 1));
}

void TestMailMsg::cfbFatSelfLoop()
{
    CfbLayout layout;
    QByteArray file = buildCfb(flatTree({{QStringLiteral("big"), 10000}}), {}, &layout);
    const quint32 start = layout.items.at(layout.idOf(QStringLiteral("big"))).start;
    cfbPut32(file, layout.fatEntryOffset(start + 2), start + 2);
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "loops");
    // The first sector pointing at itself.
    file = buildCfb(flatTree({{QStringLiteral("big"), 10000}}), {}, &layout);
    cfbPut32(file, layout.fatEntryOffset(start), start);
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "loops");
}

void TestMailMsg::cfbFatLongerCycle()
{
    CfbLayout layout;
    QByteArray file = buildCfb(flatTree({{QStringLiteral("big"), 10000}}), {}, &layout);
    const quint32 start = layout.items.at(layout.idOf(QStringLiteral("big"))).start;
    cfbPut32(file, layout.fatEntryOffset(start + 5), start + 1);
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "loops");
    // A loop in the directory chain itself.
    file = buildCfb(sampleTree(), {}, &layout);
    cfbPut32(file, layout.fatEntryOffset(layout.dirSectors.at(3)), layout.dirSectors.at(1));
    EXPECT_ERROR(openError(file), "loops");
}

void TestMailMsg::cfbOutOfRangeSectors()
{
    CfbLayout layout;
    const QByteArray good = buildCfb(flatTree({{QStringLiteral("big"), 10000}, {QStringLiteral("small"), 300}}), {}, &layout);
    const int big = layout.idOf(QStringLiteral("big"));
    const quint32 start = layout.items.at(big).start;
    QByteArray file = good;
    cfbPut32(file, layout.dirEntryOffset(big) + 116, 0x00FFFFFF);
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "out of range");
    for (const quint32 value : {0xFFFFFFFBu, kCfbFreeSect, kCfbFatSect, kCfbDifSect}) {
        file = good;
        cfbPut32(file, layout.fatEntryOffset(start + 1), value);
        EXPECT_ERROR(readError(file, QStringLiteral("big")), "out of range");
    }
    file = good;
    cfbPut32(file, layout.fatEntryOffset(start + 1), kCfbEndOfChain);   // chain too short
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "truncated");
    file = good;
    cfbPut32(file, layout.dirEntryOffset(0) + 116, 0x00FFFFFF);   // the mini stream
    EXPECT_ERROR(openError(file), "mini stream");
    file = good;
    cfbPut32(file, 0x3C, 0x00FFFFFF);   // the mini FAT
    EXPECT_ERROR(openError(file), "mini FAT");
}

void TestMailMsg::cfbDirectorySiblingCycle()
{
    // Sorted a b c d → b is the tree root, a its left child, c its right
    // child, d c's right child; ids: root 0, a 1, b 2, c 3, d 4; 5..7 free.
    CfbLayout layout;
    const QByteArray good = buildCfb(flatTree({{QStringLiteral("a"), 10}, {QStringLiteral("b"), 10},
                                                {QStringLiteral("c"), 10}, {QStringLiteral("d"), 10}}), {}, &layout);
    QVERIFY(readEverything(good).isEmpty());
    const struct {
        int entry;
        int field;
        quint32 value;
        const char *needle;
    } cases[] = {
        {1, 68, 2, "loops"},          // a.left → b
        {4, 72, 4, "loops"},          // d.right → d
        {4, 68, 2, "loops"},          // d.left → b
        {2, 72, 0, "loops"},          // b.right → the root
        {0, 76, 0, "loops"},          // root.child → the root
        {1, 68, 6, "missing"},        // an unallocated entry
        {1, 72, 70000, "missing"},    // past the directory
    };
    for (const auto &c : cases) {
        QByteArray file = good;
        cfbPut32(file, layout.dirEntryOffset(c.entry) + c.field, c.value);
        EXPECT_ERROR(openError(file), c.needle);
    }
}

void TestMailMsg::cfbChildPointsAtItself()
{
    CfbLayout layout;
    const QByteArray good = buildCfb(CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::folder(QStringLiteral("dir"), {CfbNode::stream(QStringLiteral("x"), "payload")})}), {}, &layout);
    const int dir = layout.idOf(QStringLiteral("dir"));
    const int stream = layout.idOf(QStringLiteral("x"));
    QByteArray file = good;
    cfbPut32(file, layout.dirEntryOffset(dir) + 76, quint32(dir));
    EXPECT_ERROR(openError(file), "loops");
    file = good;
    cfbPut32(file, layout.dirEntryOffset(dir) + 76, 0);   // child → the root
    EXPECT_ERROR(openError(file), "loops");
    // A stream's child link is never followed (streams have no children).
    file = good;
    cfbPut32(file, layout.dirEntryOffset(stream) + 76, quint32(stream));
    QVERIFY2(readEverything(file).isEmpty(), qPrintable(readEverything(file)));
}

void TestMailMsg::cfbAbsurdStreamSizes()
{
    CfbLayout layout;
    const QByteArray good = buildCfb(flatTree({{QStringLiteral("big"), 5000}, {QStringLiteral("small"), 100}}), {}, &layout);
    const int big = layout.idOf(QStringLiteral("big"));
    const int small = layout.idOf(QStringLiteral("small"));
    QByteArray file = good;
    cfbPut32(file, layout.dirEntryOffset(big) + 120, 0x7FFFFFF0);
    QVERIFY(openError(file).isEmpty());
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "64 MiB");
    // MS-CFB 2.6.3: in version 3 the high dword of the size is ignored.
    file = good;
    cfbPut32(file, layout.dirEntryOffset(big) + 124, 0xDEADBEEF);
    QVERIFY2(readEverything(file).isEmpty(), qPrintable(readEverything(file)));
    file = good;
    cfbPut32(file, layout.dirEntryOffset(big) + 120, 40000);   // longer than its chain
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "truncated");
    file = good;
    cfbPut32(file, layout.dirEntryOffset(small) + 120, 3000);   // longer than its mini chain
    QVERIFY(!readError(file, QStringLiteral("small")).isEmpty());
    file = good;
    cfbPut32(file, layout.dirEntryOffset(0) + 120, 0x7FFFFFF0);   // the mini stream
    EXPECT_ERROR(openError(file), "mini stream");

    CfbLayout layout4;
    const QByteArray good4 = buildCfb(flatTree({{QStringLiteral("big"), 5000}}), {4, 0}, &layout4);
    file = good4;
    cfbPut64(file, layout4.dirEntryOffset(layout4.idOf(QStringLiteral("big"))) + 120, quint64(1) << 40);
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "64 MiB");
    file = good4;
    cfbPut64(file, layout4.dirEntryOffset(layout4.idOf(QStringLiteral("big"))) + 120, ~quint64(0));
    EXPECT_ERROR(readError(file, QStringLiteral("big")), "64 MiB");
}

void TestMailMsg::cfbMiniFatOutsideMiniStream()
{
    // m1: mini sectors 0-1, m2: mini sectors 2-5; the mini stream is 384 bytes.
    CfbLayout layout;
    const QByteArray good = buildCfb(flatTree({{QStringLiteral("m1"), 100}, {QStringLiteral("m2"), 200}}), {}, &layout);
    const int m2 = layout.idOf(QStringLiteral("m2"));
    QCOMPARE(layout.items.at(m2).start, 2u);
    QVERIFY(layout.items.at(m2).mini);
    QByteArray file = good;
    cfbPut32(file, layout.miniFatEntryOffset(0), 1000);
    EXPECT_ERROR(readError(file, QStringLiteral("m1")), "outside the mini stream");
    file = good;
    cfbPut32(file, layout.dirEntryOffset(m2) + 116, 6);
    EXPECT_ERROR(readError(file, QStringLiteral("m2")), "outside the mini stream");
    file = good;
    cfbPut32(file, layout.miniFatEntryOffset(2), 2);
    EXPECT_ERROR(readError(file, QStringLiteral("m2")), "loops");
    file = good;
    cfbPut32(file, layout.miniFatEntryOffset(3), kCfbEndOfChain);
    EXPECT_ERROR(readError(file, QStringLiteral("m2")), "truncated");
    file = good;
    cfbPut32(file, layout.dirEntryOffset(0) + 120, 128);   // mini stream shorter than its users
    EXPECT_ERROR(readError(file, QStringLiteral("m2")), "mini stream");
}

void TestMailMsg::cfbDifatSectors_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<int>("fatSectors");
    QTest::addColumn<int>("difatSectors");
    QTest::newRow("v3, 300 FAT sectors") << 3 << 300 << 2;
    QTest::newRow("v4, 120 FAT sectors") << 4 << 120 << 1;
}

void TestMailMsg::cfbDifatSectors()
{
    QFETCH(int, version);
    QFETCH(int, fatSectors);
    QFETCH(int, difatSectors);
    const CfbNode tree = sampleTree();
    CfbLayout layout;
    const QByteArray file = buildCfb(tree, {version, fatSectors}, &layout);
    QCOMPARE(layout.fatSectors.size(), qsizetype(fatSectors));
    QCOMPARE(layout.difatSectors.size(), qsizetype(difatSectors));
    CfbReader reader;
    QString error;
    QVERIFY2(reader.open(file, &error), qPrintable(error));
    verifyTree(reader, reader.root(), tree);
}

void TestMailMsg::cfbDifatCorruption()
{
    CfbLayout layout;
    const QByteArray good = buildCfb(sampleTree(), {3, 300}, &layout);
    QCOMPARE(layout.difatSectors.size(), qsizetype(2));
    const qint64 next = layout.sectorOffset(layout.difatSectors.at(0)) + 127 * 4;
    QByteArray file = good;
    cfbPut32(file, next, layout.difatSectors.at(0));
    EXPECT_ERROR(openError(file), "DIFAT chain loops");
    file = good;
    cfbPut32(file, next, 0x00FFFFFF);
    EXPECT_ERROR(openError(file), "out of range");
    file = good;
    cfbPut32(file, next, kCfbEndOfChain);
    EXPECT_ERROR(openError(file), "lists only");
    file = good;
    cfbPut32(file, 0x44, kCfbFreeSect);   // the header's first DIFAT sector
    EXPECT_ERROR(openError(file), "lists only");
    file = good;
    cfbPut32(file, layout.sectorOffset(layout.difatSectors.at(1)) + 4, 0x00FFFFFF);   // a listed FAT sector
    EXPECT_ERROR(openError(file), "FAT sector");
    file = good;
    cfbPut32(file, 0x4C + 4 * 5, 0x00FFFFFF);   // a header DIFAT entry
    EXPECT_ERROR(openError(file), "FAT sector");
}

void TestMailMsg::cfbLargeFileNeedsDifat()
{
    // ~7.2 MB in 512-byte sectors needs more FAT sectors than the header's
    // 109 DIFAT slots can list.
    const QByteArray big = pattern(7200000, 77);
    const CfbNode tree = CfbNode::folder(QStringLiteral("Root Entry"),
        {CfbNode::stream(QStringLiteral("big"), big), CfbNode::stream(QStringLiteral("tiny"), "tiny")});
    CfbLayout layout;
    const QByteArray file = buildCfb(tree, {}, &layout);
    QVERIFY(layout.fatSectors.size() > 109);
    QVERIFY(!layout.difatSectors.isEmpty());
    CfbReader reader;
    QString error;
    QVERIFY2(reader.open(file, &error), qPrintable(error));
    QByteArray data;
    QVERIFY2(reader.read(reader.child(0, QStringLiteral("big")), &data, &error), qPrintable(error));
    QVERIFY(data == big);
    QVERIFY2(reader.read(reader.child(0, QStringLiteral("tiny")), &data, &error), qPrintable(error));
    QCOMPARE(data, QByteArray("tiny"));
}

void TestMailMsg::cfbTruncationFuzz()
{
    QRandomGenerator rng(0x5eed);
    for (const int version : {3, 4}) {
        const QByteArray file = buildCfb(sampleTree(), {version, version == 3 ? 120 : 0});
        QVERIFY(readEverything(file).isEmpty());
        for (int i = 0; i < 100; ++i) {
            const qsizetype length = rng.bounded(int(file.size()));
            const QString result = readEverything(file.left(length));
            QVERIFY2(!result.startsWith(u'('), qPrintable(QStringLiteral("%1 at %2").arg(result).arg(length)));
            if (length < 512) QVERIFY(!result.isEmpty());
        }
    }
}

void TestMailMsg::cfbByteFlipFuzz()
{
    QRandomGenerator rng(0xf11b);
    const QByteArray file = buildCfb(sampleTree(), {3, 0});
    int failures = 0;
    for (int i = 0; i < 400; ++i) {
        QByteArray copy = file;
        const int flips = 1 + rng.bounded(8);
        for (int k = 0; k < flips; ++k) {
            // Mostly structure: header, FAT, directory and mini FAT live up front.
            const qsizetype at = rng.bounded(int(qMin<qsizetype>(copy.size(), 9000)));
            copy[at] = char(rng.bounded(256));
        }
        const QString result = readEverything(copy);
        QVERIFY2(!result.startsWith(u'('), qPrintable(result));
        if (!result.isEmpty()) ++failures;
    }
    QVERIFY(failures > 0);
}
