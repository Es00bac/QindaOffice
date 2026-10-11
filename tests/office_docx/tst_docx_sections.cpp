// SPDX-License-Identifier: GPL-3.0-or-later
// M88 gates for Word sections in office_docx (D-096): a portrait / landscape
// / portrait document with its own margins, headers, a page-number restart
// and two columns per section, read into the section model (docx_sections.h),
// written, schema-valid and read back equal; untouched sections kept byte
// for byte while an edited one is patched; sections made in code with
// linked and unlinked headers; odd and even page section breaks; and
// LibreOffice rendering the written file with each page its section's size.

#include "docx_fixture.h"
#include "docx_format.h"
#include "docx_sections_fixture.h"
#include "docx_properties.h"
#include "docx_roundtrip.h"
#include "docx_sections.h"
#include "ooxml_namespaces.h"

#include <QProcess>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>

#include <functional>

using namespace QindaOffice;
using Test::mixedDocument;
using Test::para;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

// Every w:sectPr of a document part, in order, with relationship ids
// blanked (the writer numbers relationships afresh).
QStringList sectionMarkup(const QByteArray &bytes)
{
    QStringList out;
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(Test::partOf(bytes, QStringLiteral("word/document.xml")));
    std::function<void(Ooxml::XmlElement &)> walk = [&](Ooxml::XmlElement &e) {
        for (Ooxml::XmlChild &c : e.children) {
            if (!c.element) {
                continue;
            }
            if (c.element->is(kW, u"sectPr")) {
                std::function<void(Ooxml::XmlElement &)> blank = [&](Ooxml::XmlElement &x) {
                    if (x.hasAttribute(kR, u"id")) {
                        x.setAttribute(kR, QStringLiteral("id"), QString());
                    }
                    for (Ooxml::XmlChild &k : x.children) {
                        if (k.element) {
                            blank(*k.element);
                        }
                    }
                };
                blank(*c.element);
                out.append(Ooxml::serializeFragment(*c.element));
            } else {
                walk(*c.element);
            }
        }
    };
    if (root) {
        walk(*root);
    }
    return out;
}

QString storyText(const Docx::HeaderFooter *story)
{
    return story != nullptr && story->document ? story->document->toPlainText() : QString();
}

// pdfinfo's page sizes in points ("612 x 792"), or empty without pdfinfo.
QStringList pageSizes(const QString &pdf, int pages)
{
    QProcess info;
    info.start(QStringLiteral("pdfinfo"), {QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"),
                                           QString::number(pages), pdf});
    if (!info.waitForFinished(30000)) {
        return {};
    }
    QStringList sizes;
    static const QRegularExpression kSize(QStringLiteral("Page\\s+\\d+ size:\\s+([\\d.]+) x ([\\d.]+) pts"));
    for (auto it = kSize.globalMatch(QString::fromUtf8(info.readAllStandardOutput())); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        sizes.append(QStringLiteral("%1x%2").arg(qRound(m.captured(1).toDouble())).arg(qRound(m.captured(2).toDouble())));
    }
    return sizes;
}

} // namespace

class DocxSectionsTest : public QObject {
    Q_OBJECT
private slots:
    void settingsRoundTripThroughMaps();
    void readsEverySection();
    void unchangedSaveKeepsEverySection();
    void editedSectionIsPatchedOthersKept();
    void sectionsMadeInCodeWithLinkedHeaders();
    void oddAndEvenPageBreaks();
    void pageNumberFormats();
    void libreOfficeRendersEachPageAtItsSize();
};

void DocxSectionsTest::settingsRoundTripThroughMaps()
{
    Docx::SectionSettings s = Docx::readSectionSettings(Ooxml::parseFragment(
        QStringLiteral("<w:sectPr xmlns:w=\"%1\"><w:type w:val=\"oddPage\"/><w:pgSz w:w=\"15840\" w:h=\"12240\""
                       " w:orient=\"landscape\"/><w:pgMar w:top=\"720\" w:right=\"1440\" w:bottom=\"720\" w:left=\"2880\""
                       " w:header=\"360\" w:footer=\"360\" w:gutter=\"360\"/><w:pgNumType w:fmt=\"upperLetter\""
                       " w:start=\"3\" w:chapStyle=\"1\" w:chapSep=\"emDash\"/><w:cols w:num=\"3\" w:space=\"360\""
                       " w:sep=\"1\"/><w:vAlign w:val=\"center\"/><w:titlePg/></w:sectPr>")
            .arg(kW)).get());
    QCOMPARE(s.layout.pageSize, QSizeF(1056, 816));
    QVERIFY(s.layout.landscape());
    QCOMPARE(s.layout.margins, QMarginsF(192, 48, 96, 48));
    QCOMPARE(s.layout.gutter, 24.0);
    QCOMPARE(s.flow.start, Docx::SectionStart::OddPage);
    QCOMPARE(s.flow.numbering.start, 3);
    QCOMPARE(s.flow.numbering.format, QStringLiteral("upperLetter"));
    QCOMPARE(s.flow.numbering.chapterStyle, 1);
    QCOMPARE(s.flow.columns.count, 3);
    QVERIFY(s.flow.columns.separator);
    QCOMPARE(s.flow.verticalAlign, QStringLiteral("center"));
    QVERIFY(s.titlePage);
    QVERIFY(Docx::SectionSettings::fromMap(s.toMap()) == s);
    // A new element says the same.
    QVERIFY(Docx::readSectionSettings(Docx::newSectionElement(s).get()) == s);
}

void DocxSectionsTest::readsEverySection()
{
    auto doc = Test::readWord(mixedDocument());
    QVERIFY(doc);
    const QList<Docx::SectionRef> sections = Docx::documentSections(doc->body, doc->parts);
    QCOMPARE(sections.size(), 3);
    QCOMPARE(sections[0].end.text(), QStringLiteral("End of the introduction."));
    QCOMPARE(sections[1].end.text(), QStringLiteral("End of the appendix."));
    QVERIFY(sections[2].closing());
    QCOMPARE(sections[0].settings.layout.pageSize, QSizeF(816, 1056));
    QCOMPARE(sections[1].settings.layout.pageSize, QSizeF(1056, 816));
    QVERIFY(sections[1].settings.layout.landscape());
    QCOMPARE(sections[1].settings.layout.margins, QMarginsF(72, 72, 72, 72));
    QCOMPARE(sections[2].settings.layout.margins.left(), 120.0);
    QVERIFY(sections[0].settings.titlePage);
    QCOMPARE(sections[0].settings.flow.numbering.format, QStringLiteral("lowerRoman"));
    QCOMPARE(sections[1].settings.flow.numbering.start, 1);
    QCOMPARE(sections[2].settings.flow.columns.count, 2);
    QCOMPARE(doc->parts.layout, sections[2].settings.layout); // the closing section is WordParts'

    // Text lies in its section; the section ends never break pages themselves.
    const QTextBlock appendix = doc->body.findBlockByNumber(sections[0].end.blockNumber() + 1);
    QCOMPARE(appendix.text(), QStringLiteral("Appendix A: the figures"));
    QCOMPARE(Docx::sectionIndexAt(sections, appendix.position()), 1);
    QCOMPARE(Docx::sectionIndexAt(sections, doc->body.lastBlock().position()), 2);
    QVERIFY(!(sections[0].end.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysAfter));

    // Headers and footers: own, linked to the previous, or none at all.
    using Slot = Docx::HeaderFooterSlot;
    const Docx::WordParts &parts = doc->parts;
    QCOMPARE(storyText(Docx::sectionStory(parts, sections[0].key, false, Slot::Default)), QStringLiteral("Report header"));
    QVERIFY(Docx::sectionStory(parts, sections[0].key, false, Slot::First)->defines()); // empty but its own
    QCOMPARE(storyText(Docx::sectionStory(parts, sections[1].key, false, Slot::Default)),
             QStringLiteral("Landscape appendix"));
    QCOMPARE(Docx::storyOwner(parts, sections, 1, true, Slot::Default), 0); // footer linked to section 1
    QCOMPARE(Docx::storyOwner(parts, sections, 2, false, Slot::Default), 1); // header linked to section 2
    QCOMPARE(Docx::storyOwner(parts, sections, 2, true, Slot::Default), 2);
    QCOMPARE(Docx::storyOwner(parts, sections, 1, false, Slot::Even), -1);
}

void DocxSectionsTest::unchangedSaveKeepsEverySection()
{
    const QByteArray source = mixedDocument();
    auto doc = Test::readWord(source);
    QVERIFY(doc);
    QString error;
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts, {}, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    // Byte for byte, relationship ids aside.
    QCOMPARE(sectionMarkup(bytes), sectionMarkup(source));
    // Every header and footer part copied as it was.
    for (const QString &name : {QStringLiteral("word/header1.xml"), QStringLiteral("word/header3.xml"),
                                QStringLiteral("word/footer2.xml")}) {
        QCOMPARE(Test::partOf(bytes, name), Test::partOf(source, name));
    }
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    const QList<Docx::SectionRef> before = Docx::documentSections(doc->body, doc->parts);
    const QList<Docx::SectionRef> after = Docx::documentSections(back->body, back->parts);
    QCOMPARE(after.size(), before.size());
    for (qsizetype i = 0; i < before.size(); ++i) {
        QVERIFY(after[i].settings == before[i].settings);
        QCOMPARE(after[i].end.text(), before[i].end.text());
    }
    QCOMPARE(back->body.toPlainText(), doc->body.toPlainText());
}

void DocxSectionsTest::editedSectionIsPatchedOthersKept()
{
    const QByteArray source = mixedDocument();
    auto doc = Test::readWord(source);
    QVERIFY(doc);
    QList<Docx::SectionRef> sections = Docx::documentSections(doc->body, doc->parts);
    // The landscape section's margins and a three-column closing section.
    Docx::SectionSettings wide = sections[1].settings;
    wide.layout.margins = QMarginsF(48, 48, 48, 48);
    wide.flow.numbering.format = QStringLiteral("upperRoman");
    QTextBlockFormat format = sections[1].end.blockFormat();
    format.setProperty(Docx::kSectionSettings, wide.toMap());
    QTextCursor(sections[1].end).setBlockFormat(format);
    Docx::SectionSettings last = Docx::closingSettings(doc->parts);
    last.flow.columns.count = 3;
    last.flow.start = Docx::SectionStart::Continuous;
    Docx::setClosingSettings(doc->parts, last);

    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QStringList now = sectionMarkup(bytes);
    const QStringList was = sectionMarkup(source);
    QCOMPARE(now.size(), 3);
    QCOMPARE(now[0], was[0]); // untouched: byte for byte
    QVERIFY2(now[1].contains(QStringLiteral("w:top=\"720\" w:right=\"720\"")), qPrintable(now[1]));
    QVERIFY(now[1].contains(QStringLiteral("w:code=\"1\""))); // the paper is unchanged
    QVERIFY(now[1].contains(QStringLiteral("<w:lnNumType w:countBy=\"5\"/>"))); // not modelled: kept
    QVERIFY(now[1].contains(QStringLiteral("w:fmt=\"upperRoman\"")));
    QVERIFY(now[1].contains(QStringLiteral("w:start=\"1\"")));
    QVERIFY(now[1].contains(QStringLiteral("<w:headerReference")));
    QVERIFY2(now[2].contains(QStringLiteral("<w:type w:val=\"continuous\"/>")), qPrintable(now[2]));
    QVERIFY(now[2].contains(QStringLiteral("w:num=\"3\"")));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    sections = Docx::documentSections(back->body, back->parts);
    QVERIFY(sections[1].settings == wide);
    QVERIFY(sections[2].settings == last);
    QCOMPARE(storyText(Docx::sectionStory(back->parts, sections[1].key, false, Docx::HeaderFooterSlot::Default)),
             QStringLiteral("Landscape appendix"));
}

void DocxSectionsTest::sectionsMadeInCodeWithLinkedHeaders()
{
    QTextDocument body;
    Docx::WordParts parts;
    QTextCursor cursor(&body);
    cursor.insertText(QStringLiteral("First section"));
    Docx::SectionSettings landscape;
    landscape.layout.pageSize = QSizeF(1056, 816);
    landscape.flow.numbering.start = 5;
    QTextBlockFormat end = cursor.blockFormat();
    const QString first = Docx::newSectionKey();
    Docx::markSectionEnd(end, first, Ooxml::serializeFragment(*Docx::newSectionElement(Docx::SectionSettings())));
    cursor.setBlockFormat(end);
    cursor.insertBlock(QTextBlockFormat());
    cursor.insertText(QStringLiteral("Second section"));
    QTextBlockFormat second = cursor.blockFormat();
    const QString key = Docx::newSectionKey();
    QVERIFY(key != first);
    Docx::markSectionEnd(second, key, Ooxml::serializeFragment(*Docx::newSectionElement(landscape)));
    cursor.setBlockFormat(second);
    // Enter at the end of the section's paragraph copies its break into
    // the new one: of the two, the last ends the section.
    cursor.insertBlock();
    cursor.insertText(QStringLiteral("Second section, continued"));
    cursor.insertBlock(QTextBlockFormat());
    cursor.insertText(QStringLiteral("Closing section"));
    QCOMPARE(Docx::sectionEnds(body).size(), 2);
    QCOMPARE(Docx::sectionEnds(body).last().text(), QStringLiteral("Second section, continued"));

    using Slot = Docx::HeaderFooterSlot;
    Docx::HeaderFooter *mine = Docx::sectionStory(parts, first, false, Slot::Default, true);
    mine->document = std::make_unique<QTextDocument>();
    QTextCursor(mine->document.get()).insertText(QStringLiteral("Shared header"));
    // The second section turned Link to Previous off and left its header empty.
    Docx::HeaderFooter *own = Docx::sectionStory(parts, key, false, Slot::Default, true);
    own->document = std::make_unique<QTextDocument>();
    own->unlinked = true;

    const QByteArray bytes = Docx::docxBytes(body, parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    const QList<Docx::SectionRef> sections = Docx::documentSections(back->body, back->parts);
    QCOMPARE(sections.size(), 3);
    QCOMPARE(sections[1].end.text(), QStringLiteral("Second section, continued"));
    QCOMPARE(sections[1].settings.layout.pageSize, QSizeF(1056, 816));
    QCOMPARE(sections[1].settings.flow.numbering.start, 5);
    QCOMPARE(storyText(Docx::sectionStory(back->parts, sections[0].key, false, Slot::Default)),
             QStringLiteral("Shared header"));
    const Docx::HeaderFooter *empty = Docx::sectionStory(back->parts, sections[1].key, false, Slot::Default);
    QVERIFY(empty != nullptr && empty->defines() && empty->isEmpty());
    // The closing section links to the empty one, not to the first.
    QCOMPARE(Docx::storyOwner(back->parts, sections, 2, false, Slot::Default), 1);
}

void DocxSectionsTest::oddAndEvenPageBreaks()
{
    Test::DocxFixture f;
    const QString odd = QStringLiteral("<w:sectPr><w:type w:val=\"oddPage\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                                       "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\""
                                       " w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>");
    f.body = para(QStringLiteral("Front matter"))
        + QStringLiteral("<w:p><w:pPr><w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr></w:pPr>"
                         "<w:r><w:t>End of the front matter</w:t></w:r></w:p>")
        + para(QStringLiteral("Chapter one"))
        + QStringLiteral("<w:p><w:pPr>%1</w:pPr><w:r><w:t>End of chapter one</w:t></w:r></w:p>").arg(odd)
        + para(QStringLiteral("Chapter two"))
        + QStringLiteral("<w:sectPr><w:type w:val=\"evenPage\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr>");
    const QByteArray source = f.build();
    auto doc = Test::readWord(source);
    QVERIFY(doc);
    QList<Docx::SectionRef> sections = Docx::documentSections(doc->body, doc->parts);
    QCOMPARE(sections.size(), 3);
    QCOMPARE(sections[0].settings.flow.start, Docx::SectionStart::NextPage);
    QCOMPARE(sections[1].settings.flow.start, Docx::SectionStart::OddPage);
    QCOMPARE(sections[2].settings.flow.start, Docx::SectionStart::EvenPage);
    // Chapter one now starts on an even page, chapter two on the next page.
    Docx::SectionSettings one = sections[1].settings;
    one.flow.start = Docx::SectionStart::EvenPage;
    QTextBlockFormat format = sections[1].end.blockFormat();
    format.setProperty(Docx::kSectionSettings, one.toMap());
    QTextCursor(sections[1].end).setBlockFormat(format);
    Docx::SectionSettings two = Docx::closingSettings(doc->parts);
    two.flow.start = Docx::SectionStart::NextPage;
    Docx::setClosingSettings(doc->parts, two);
    const QByteArray bytes = Docx::docxBytes(doc->body, doc->parts);
    QString problems;
    QVERIFY2(Test::validates(bytes, &problems), qPrintable(problems));
    const QStringList markup = sectionMarkup(bytes);
    QCOMPARE(markup.size(), 3);
    QVERIFY(markup[1].contains(QStringLiteral("<w:type w:val=\"evenPage\"/>")));
    QVERIFY(!markup[2].contains(QStringLiteral("w:type")));
    auto back = Test::readWord(bytes);
    QVERIFY(back);
    sections = Docx::documentSections(back->body, back->parts);
    QCOMPARE(sections[1].settings.flow.start, Docx::SectionStart::EvenPage);
    QCOMPARE(sections[2].settings.flow.start, Docx::SectionStart::NextPage);
}

void DocxSectionsTest::pageNumberFormats()
{
    QCOMPARE(Docx::formatPageNumber(4, QString()), QStringLiteral("4"));
    QCOMPARE(Docx::formatPageNumber(4, QStringLiteral("lowerRoman")), QStringLiteral("iv"));
    QCOMPARE(Docx::formatPageNumber(9, QStringLiteral("upperRoman")), QStringLiteral("IX"));
    QCOMPARE(Docx::formatPageNumber(2, QStringLiteral("lowerLetter")), QStringLiteral("b"));
    QCOMPARE(Docx::formatPageNumber(28, QStringLiteral("upperLetter")), QStringLiteral("BB"));
    QCOMPARE(Docx::formatPageNumber(3, QStringLiteral("numberInDash")), QStringLiteral("- 3 -"));
    QCOMPARE(Docx::fieldNumberFormat(QStringLiteral("PAGE \\* roman \\* MERGEFORMAT")), QStringLiteral("lowerRoman"));
    QCOMPARE(Docx::fieldNumberFormat(QStringLiteral("PAGE \\* MERGEFORMAT")), QString());
    // The display-only right margin: the own one survives a pad and a command.
    QTextBlockFormat block;
    block.setRightMargin(12);
    Docx::setSectionPad(block, 240);
    QCOMPARE(block.rightMargin(), 252.0);
    QCOMPARE(Docx::ownRightMargin(block), 12.0);
    block.setRightMargin(30); // a command set the margin outright
    QCOMPARE(Docx::ownRightMargin(block), 30.0);
    Docx::setSectionPad(block, 100);
    QCOMPARE(block.rightMargin(), 130.0);
    Docx::setOwnRightMargin(block, 0);
    QCOMPARE(block.rightMargin(), 100.0);
    QCOMPARE(Docx::ownRightMargin(block), 0.0);
    Docx::setSectionPad(block, 0);
    QVERIFY(!block.hasProperty(QTextFormat::BlockRightMargin));
    QCOMPARE(Docx::blockSnapshot(block).value(QStringLiteral("right")).toInt(), 0);
}

void DocxSectionsTest::libreOfficeRendersEachPageAtItsSize()
{
    if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("pdfinfo")).isEmpty()) {
        QSKIP("LibreOffice or pdfinfo is not installed");
    }
    auto doc = Test::readWord(mixedDocument());
    QVERIFY(doc);
    QTemporaryDir scratch;
    const QString pdf = Test::renderWithLibreOffice(Docx::docxBytes(doc->body, doc->parts),
                                                    QStringLiteral("m88_sections"), scratch);
    QVERIFY(!pdf.isEmpty());
    const QStringList sizes = pageSizes(pdf, 12);
    QVERIFY2(sizes.size() >= 4, qPrintable(sizes.join(QLatin1Char(' '))));
    // Portrait pages, then the landscape appendix, then portrait again.
    const QString portrait = QStringLiteral("612x792");
    const QString landscape = QStringLiteral("792x612");
    QCOMPARE(sizes.first(), portrait);
    const qsizetype wide = sizes.indexOf(landscape);
    QVERIFY2(wide > 0, qPrintable(sizes.join(QLatin1Char(' '))));
    const qsizetype back = sizes.indexOf(portrait, wide);
    QVERIFY2(back > wide + 1, qPrintable(sizes.join(QLatin1Char(' ')))); // the table runs over 2+ landscape pages
    for (qsizetype i = wide; i < back; ++i) {
        QCOMPARE(sizes.at(i), landscape);
    }
    for (qsizetype i = back; i < sizes.size(); ++i) {
        QCOMPARE(sizes.at(i), portrait);
    }
    const QString text = Test::pdfText(pdf);
    if (!text.isEmpty()) {
        QVERIFY(text.contains(QStringLiteral("Landscape appendix")));
        QVERIFY(text.contains(QStringLiteral("Report header")));
    }
}

QTEST_MAIN(DocxSectionsTest)
#include "tst_docx_sections.moc"
