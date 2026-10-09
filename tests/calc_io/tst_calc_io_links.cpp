// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): cell hyperlinks as Excel writes them — an external address
// through a sheet relationship (TargetMode="External"), a mailto: link, an
// internal location ('Q1 Sales'!B2) with no relationship at all, a range
// link whose address carries a #fragment in the location attribute, display
// text and a tooltip. Before M66 every one of them was lost on save.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;
using QindaOffice::Test::ExcelRel;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::officeRel;

class CalcIoLinksTest : public QObject {
    Q_OBJECT
private slots:
    void excelHyperlinksSurviveSave();
    void savedLinksReadBackTheSame();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
};

namespace {

// The sheet the way Excel 365 writes it: revision uids on each link (the
// xr namespace is ignorable), the external targets in the sheet's rels.
ExcelWorkbook linkBook()
{
    ExcelWorkbook book;
    book.sharedStrings = {QByteArrayLiteral("<t>Example site</t>"), QByteArrayLiteral("<t>Mail us</t>"),
                          QByteArrayLiteral("<t>Go to Q1</t>"), QByteArrayLiteral("<t>Install docs</t>")};
    ExcelSheet links;
    links.name = QStringLiteral("Links");
    links.rootAttributes = QByteArrayLiteral(
        " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"xr\""
        " xmlns:xr=\"http://schemas.microsoft.com/office/spreadsheetml/2014/revision\"");
    links.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>1</v></c></row>"
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>2</v></c></row>"
        "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>3</v></c></row>");
    links.afterData = QByteArrayLiteral(
        "<hyperlinks>"
        "<hyperlink ref=\"A1\" r:id=\"rId1\" tooltip=\"Open the example\" display=\"Example site\""
        " xr:uid=\"{1A2B3C4D-0000-4000-8000-000000000001}\"/>"
        "<hyperlink ref=\"A2\" r:id=\"rId2\" xr:uid=\"{1A2B3C4D-0000-4000-8000-000000000002}\"/>"
        "<hyperlink ref=\"A3\" location=\"'Q1 Sales'!B2\" display=\"Q1 Sales\""
        " xr:uid=\"{1A2B3C4D-0000-4000-8000-000000000003}\"/>"
        "<hyperlink ref=\"A4:B4\" r:id=\"rId3\" location=\"install\""
        " xr:uid=\"{1A2B3C4D-0000-4000-8000-000000000004}\"/>"
        "</hyperlinks>"
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
    links.rels = {
        {QStringLiteral("rId1"), officeRel("hyperlink"), QStringLiteral("https://example.com/"), true},
        {QStringLiteral("rId2"), officeRel("hyperlink"), QStringLiteral("mailto:sales@example.com?subject=Order"), true},
        {QStringLiteral("rId3"), officeRel("hyperlink"), QStringLiteral("https://example.com/docs"), true},
    };
    ExcelSheet sales;
    sales.name = QStringLiteral("Q1 Sales");
    sales.sheetData = QByteArrayLiteral("<row r=\"2\"><c r=\"B2\"><v>42</v></c></row>");
    book.sheets = {links, sales};
    return book;
}

struct SavedLink {
    QString ref;
    QString target; // the relationship's target, "" for an internal link
    QString targetMode;
    QString location;
    QString display;
    QString tooltip;
};

// The <hyperlinks> of a saved sheet part with each r:id resolved.
QList<SavedLink> savedLinks(const Package &package, const QString &sheetPart)
{
    QList<SavedLink> out;
    const QindaOffice::Ooxml::XmlElementPtr root = package.xml(sheetPart);
    const XmlElement *links = root ? root->child(QindaOffice::Test::sml(), u"hyperlinks") : nullptr;
    if (links == nullptr) {
        return out;
    }
    const QString r = QString::fromUtf16(QindaOffice::Ooxml::Ns::R);
    for (const XmlElement *link : links->elements(QindaOffice::Test::sml(), u"hyperlink")) {
        SavedLink saved{link->attr(u"ref"), {}, {}, link->attr(u"location"), link->attr(u"display"),
                        link->attr(u"tooltip")};
        const QString id = link->attribute(r, u"id");
        if (!id.isEmpty()) {
            if (const auto rel = package.relationship(sheetPart, id)) {
                saved.target = rel->target;
                saved.targetMode = rel->external ? QStringLiteral("External") : QStringLiteral("Internal");
            } else {
                saved.target = QStringLiteral("<dangling %1>").arg(id);
            }
        }
        out.append(saved);
    }
    return out;
}

const SavedLink *linkAt(const QList<SavedLink> &links, const QString &ref)
{
    for (const SavedLink &link : links) {
        if (link.ref == ref) {
            return &link;
        }
    }
    return nullptr;
}

void verifyExcelLinks(const QList<SavedLink> &links)
{
    QCOMPARE(links.size(), 4);
    const SavedLink *site = linkAt(links, QStringLiteral("A1"));
    QVERIFY(site != nullptr);
    QCOMPARE(site->target, QStringLiteral("https://example.com/"));
    QCOMPARE(site->targetMode, QStringLiteral("External"));
    QCOMPARE(site->display, QStringLiteral("Example site"));
    QCOMPARE(site->tooltip, QStringLiteral("Open the example"));
    const SavedLink *mail = linkAt(links, QStringLiteral("A2"));
    QVERIFY(mail != nullptr);
    QCOMPARE(mail->target, QStringLiteral("mailto:sales@example.com?subject=Order"));
    QCOMPARE(mail->targetMode, QStringLiteral("External"));
    // An internal location has no relationship at all (Excel's shape).
    const SavedLink *internal = linkAt(links, QStringLiteral("A3"));
    QVERIFY(internal != nullptr);
    QVERIFY(internal->target.isEmpty());
    QCOMPARE(internal->location, QStringLiteral("'Q1 Sales'!B2"));
    QCOMPARE(internal->display, QStringLiteral("Q1 Sales"));
    // A range link: the address up to '#' in the relationship, the fragment
    // in the location attribute, as Excel splits it.
    const SavedLink *docs = linkAt(links, QStringLiteral("A4:B4"));
    QVERIFY(docs != nullptr);
    QCOMPARE(docs->target, QStringLiteral("https://example.com/docs"));
    QCOMPARE(docs->location, QStringLiteral("install"));
}

} // namespace

void CalcIoLinksTest::excelHyperlinksSurviveSave()
{
    const QString source = path(QStringLiteral("links.xlsx"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(linkBook().build());
    }
    QQO_VERIFY_XLSX_SCHEMA(source);
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    const QString saved = path(QStringLiteral("links-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QindaOffice::Test::keepForInspection(saved);
    const auto package = Package::load(saved, &error);
    QVERIFY2(package, qPrintable(error));
    verifyExcelLinks(savedLinks(*package, QStringLiteral("xl/worksheets/sheet1.xml")));
    // The second sheet had no links and gets no <hyperlinks> element.
    QVERIFY(savedLinks(*package, QStringLiteral("xl/worksheets/sheet2.xml")).isEmpty());
}

void CalcIoLinksTest::savedLinksReadBackTheSame()
{
    // Our own output read and written again keeps every link (no drift
    // between what the writer emits and what the reader understands).
    const QString first = path(QStringLiteral("links-first.xlsx"));
    {
        QFile file(path(QStringLiteral("links-src.xlsx")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(linkBook().build());
    }
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(path(QStringLiteral("links-src.xlsx")), &wb, &error), qPrintable(error));
    QVERIFY2(IO::writeXlsxFile(wb, first, &error), qPrintable(error));
    Workbook again;
    QVERIFY2(IO::readXlsxFile(first, &again, &error), qPrintable(error));
    const QString second = path(QStringLiteral("links-second.xlsx"));
    QVERIFY2(IO::writeXlsxFile(again, second, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(second);
    const auto package = Package::load(second, &error);
    QVERIFY2(package, qPrintable(error));
    verifyExcelLinks(savedLinks(*package, QStringLiteral("xl/worksheets/sheet1.xml")));
}

QTEST_GUILESS_MAIN(CalcIoLinksTest)
#include "tst_calc_io_links.moc"
