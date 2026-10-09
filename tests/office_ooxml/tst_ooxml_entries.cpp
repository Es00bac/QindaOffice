// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) gates for office_ooxml: a PackageWriter's entries read back
// as a Package with no zip in between (QindaShow re-resolves a deck that
// way, sharing the bytes of its media), video and audio stored rather
// than deflated, and PowerPoint's modern-comment and theme-family
// namespaces written with PowerPoint's prefixes.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QtTest>

using namespace QindaOffice::Ooxml;

class OoxmlEntriesTest : public QObject {
    Q_OBJECT
private slots:
    void entriesReadBackWithoutAZip();
    void powerPointCommentPrefixes();
};

// M75 (D-084): a writer's entries read back as a package with no zip in
// between (QindaShow re-resolves a deck that way), parts and relationships
// as fromData() sees them; video and audio are stored, not deflated.
void OoxmlEntriesTest::entriesReadBackWithoutAZip()
{
    PackageWriter writer;
    XmlElement doc(QString::fromUtf16(Ns::W), QStringLiteral("document"));
    writer.addXmlPart(QStringLiteral("word/document.xml"), doc,
                      QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
    writer.addRelationship(QString(), QString::fromUtf16(RelType::OfficeDocument), QStringLiteral("word/document.xml"));
    QByteArray clip(4096, '\0');
    for (int i = 0; i < clip.size(); ++i) {
        clip[i] = char((i * 7919 + 13) % 251); // periodic: deflated, it would not appear verbatim
    }
    writer.addPart(QStringLiteral("word/media/media1.mp4"), clip, QStringLiteral("video/mp4"));
    writer.addRelationshipWithId(QStringLiteral("word/document.xml"), QStringLiteral("rId4"),
                                 QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/media"),
                                 QStringLiteral("word/media/media1.mp4"));
    QString error;
    const auto direct = Package::fromEntries(writer.entries(), &error);
    QVERIFY2(direct, qPrintable(error));
    const QByteArray zip = writer.toZip(&error);
    const auto zipped = Package::fromData(zip, &error);
    QVERIFY2(zipped, qPrintable(error));
    QStringList directParts = direct->partNames();
    QStringList zippedParts = zipped->partNames();
    directParts.sort();
    zippedParts.sort();
    QCOMPARE(directParts, zippedParts);
    for (const QString &part : std::as_const(zippedParts)) {
        QCOMPARE(direct->data(part), zipped->data(part));
        QCOMPARE(direct->contentType(part), zipped->contentType(part));
    }
    QCOMPARE(direct->mainPart(), QStringLiteral("word/document.xml"));
    const QList<Relationship> rels = direct->relationships(QStringLiteral("word/document.xml"));
    QCOMPARE(rels.size(), 1);
    QCOMPARE(direct->targetPart(QStringLiteral("word/document.xml"), rels.first()), QStringLiteral("word/media/media1.mp4"));
    QVERIFY2(zip.contains(clip), "the video was deflated");
    // Without [Content_Types].xml it is no package.
    QVERIFY(!Package::fromEntries({{QStringLiteral("word/document.xml"), QByteArray("<w:document/>")}}, &error));
    QVERIFY(error.contains(QStringLiteral("Content_Types")));
}

// M75: PowerPoint's modern comments and theme family are written with the
// prefixes PowerPoint uses.
void OoxmlEntriesTest::powerPointCommentPrefixes()
{
    const QByteArray xml = R"(<x:cmLst xmlns:x="http://schemas.microsoft.com/office/powerpoint/2018/8/main"
 xmlns:k="http://schemas.microsoft.com/office/powerpoint/2013/main/command"
 xmlns:d="http://schemas.microsoft.com/office/drawing/2013/main/command"
 xmlns:t="http://schemas.microsoft.com/office/thememl/2012/main"><x:cm><d:deMkLst><k:docMk/></d:deMkLst><t:themeFamily/></x:cm></x:cmLst>)";
    const XmlElementPtr root = parseXml(xml);
    QVERIFY(root);
    const QByteArray out = serializeDocument(*root);
    for (const char *expected : {"<p188:cmLst", "<ac:deMkLst", "<pc:docMk", "<thm15:themeFamily"}) {
        QVERIFY2(out.contains(expected), out.constData());
    }
}

QTEST_GUILESS_MAIN(OoxmlEntriesTest)
#include "tst_ooxml_entries.moc"
