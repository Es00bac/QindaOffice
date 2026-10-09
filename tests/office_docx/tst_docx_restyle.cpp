// SPDX-License-Identifier: GPL-3.0-or-later
// Applying a Word document's own paragraph style (docx_restyle.h): the
// paragraph shows the document's Heading 1, keeps the run's direct bold,
// the next paragraph's folded spacing follows, and the saved markup is
// <w:pStyle/> only — validated with QQO_OOXML_SCHEMA_DIR.

#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_restyle.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;

namespace {

const char *kStyles =
    R"(<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>)"
    R"(<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>)"
    R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>)"
    R"(<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/>)"
    R"(<w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before="240" w:after="0"/>)"
    R"(<w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:color w:val="2F5496"/><w:sz w:val="32"/></w:rPr></w:style>)";

const char *kBody =
    R"(<w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:t xml:space="preserve">Quarterly </w:t></w:r>)"
    R"(<w:r><w:rPr><w:b/></w:rPr><w:t>report</w:t></w:r></w:p>)"
    R"(<w:p><w:r><w:t>Body text.</w:t></w:r></w:p>)";

} // namespace

class DocxRestyleTest : public QObject {
    Q_OBJECT
private slots:
    void appliesTheDocumentsOwnStyle();
};

void DocxRestyleTest::appliesTheDocumentsOwnStyle()
{
    Test::DocxFixture fixture;
    fixture.styles = QString::fromLatin1(kStyles);
    fixture.body = QString::fromLatin1(kBody);
    QString error;
    const auto package = Ooxml::Package::fromData(fixture.build(), &error);
    QVERIFY2(package, qPrintable(error));
    QTextDocument doc;
    Docx::WordParts parts;
    QVERIFY2(Docx::readDocx(package, &doc, &parts, &error), qPrintable(error));

    const Docx::DocumentStyles styles(parts);
    QVERIFY(!styles.isEmpty());
    QCOMPARE(styles.styleFor(2), QStringLiteral("Heading1")); // Write's Heading 1
    QCOMPARE(styles.styleFor(0), QStringLiteral("Normal"));
    QVERIFY(styles.styleFor(5).isEmpty()); // no Heading 4 in this document
    QVERIFY(Docx::DocumentStyles(Docx::WordParts()).isEmpty());

    const qreal bodyTopBefore = doc.begin().next().blockFormat().topMargin();
    QVERIFY(bodyTopBefore > 0); // Normal's 8 pt after, folded into it
    QTextCursor edit(&doc);
    edit.beginEditBlock();
    styles.apply(doc.begin(), QStringLiteral("Heading1"));
    edit.endEditBlock();

    const QTextBlock heading = doc.begin();
    QCOMPARE(heading.blockFormat().headingLevel(), 1);
    QVERIFY(heading.blockFormat().boolProperty(Docx::kKeepWithNext));
    QCOMPARE(heading.blockFormat().alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft); // direct jc gone
    QCOMPARE(heading.blockFormat().intProperty(Docx::kWriteStyleIdProperty), 2);
    QCOMPARE(heading.text(), QStringLiteral("Quarterly report"));
    QTextBlock::iterator run = heading.begin();
    QCOMPARE(run.fragment().charFormat().fontPointSize(), 16.0);
    QCOMPARE(run.fragment().charFormat().foreground().color(), QColor(0x2f, 0x54, 0x96));
    QVERIFY(run.fragment().charFormat().fontWeight() != QFont::Bold);
    ++run;
    QCOMPARE(run.fragment().text(), QStringLiteral("report"));
    QCOMPARE(run.fragment().charFormat().fontWeight(), int(QFont::Bold)); // the run's own bold stays
    QCOMPARE(run.fragment().charFormat().fontPointSize(), 16.0);
    // Heading 1 has no space after: the body paragraph's folded gap shrinks.
    QCOMPARE(doc.begin().next().blockFormat().topMargin(), 0.0);

    const QByteArray bytes = Docx::docxBytes(doc, parts);
    const auto written = Ooxml::Package::fromData(bytes, &error);
    QVERIFY2(written, qPrintable(error));
    const QString body = QString::fromUtf8(written->data(written->mainPart()));
    QVERIFY(body.contains(QStringLiteral("<w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>")));
    QVERIFY(!body.contains(QStringLiteral("w:jc")));
    QVERIFY(!body.contains(QStringLiteral("<w:sz ")));
    QVERIFY(body.contains(QStringLiteral("<w:b/>")));
    if (Test::schemaValidationAvailable()) {
        const QStringList errors = Test::validatePackage(*written);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
    }
    // Read back: still the document's Heading 1.
    QTextDocument again;
    Docx::WordParts againParts;
    QVERIFY2(Docx::readDocx(written, &again, &againParts, &error), qPrintable(error));
    QTextBlock first = again.begin();
    while (first.blockFormat().boolProperty(Docx::kParagraphSynthetic)) {
        first = first.next(); // the reader's spacer for Heading 1's space before
    }
    QCOMPARE(first.blockFormat().headingLevel(), 1);
    QCOMPARE(first.begin().fragment().charFormat().fontPointSize(), 16.0);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    DocxRestyleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_docx_restyle.moc"
