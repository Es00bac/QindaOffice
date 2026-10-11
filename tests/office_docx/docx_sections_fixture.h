// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M88 (D-096): a Word-shaped document with three sections for the office_docx
// and QindaWrite section gates — portrait (title page, roman page numbers,
// its own header and footer) / landscape (its own header, numbers
// restarting at 1, an 80-row six-column table) / portrait (two columns, its
// own footer with PAGE and SECTIONPAGES, the header linked to the
// landscape one). Hand-built, as Word writes it.

#include "docx_fixture.h"

namespace QindaOffice::Test {

inline QByteArray sectionPart(const QString &root, const QString &inner)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:%1%2>%3</w:%1>")
        .arg(root, QLatin1String(Test::kWordNamespaces), inner)
        .toUtf8();
}

inline void addStory(Test::DocxFixture &f, const QString &id, const QString &name, bool footer, const QString &inner)
{
    const QString t = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");
    f.extras.insert(id, {name, QString::fromUtf16(footer ? Ooxml::RelType::Footer : Ooxml::RelType::Header),
                         t + (footer ? QStringLiteral("footer+xml") : QStringLiteral("header+xml")),
                         sectionPart(footer ? QStringLiteral("ftr") : QStringLiteral("hdr"), inner)});
}

inline const char *kSection1 =
    "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdHeader\"/>"
    "<w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/><w:headerReference w:type=\"first\" r:id=\"rIdFirst\"/>"
    "<w:pgSz w:w=\"12240\" w:h=\"15840\" w:code=\"1\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\""
    " w:left=\"1440\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/><w:pgNumType w:fmt=\"lowerRoman\"/>"
    "<w:cols w:space=\"720\"/><w:titlePg/><w:docGrid w:linePitch=\"360\"/></w:sectPr>";
inline const char *kSection2 =
    "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rIdWide\"/>"
    "<w:pgSz w:w=\"15840\" w:h=\"12240\" w:orient=\"landscape\" w:code=\"1\"/><w:pgMar w:top=\"1080\""
    " w:right=\"1080\" w:bottom=\"1080\" w:left=\"1080\" w:header=\"576\" w:footer=\"576\" w:gutter=\"0\"/>"
    "<w:lnNumType w:countBy=\"5\"/><w:pgNumType w:start=\"1\"/><w:cols w:space=\"720\"/>"
    "<w:docGrid w:linePitch=\"360\"/></w:sectPr>";
inline const char *kSection3 =
    "<w:sectPr><w:footerReference w:type=\"default\" r:id=\"rIdLast\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
    "<w:pgMar w:top=\"1440\" w:right=\"1800\" w:bottom=\"1440\" w:left=\"1800\" w:header=\"720\" w:footer=\"720\""
    " w:gutter=\"0\"/><w:cols w:num=\"2\" w:space=\"720\"/><w:docGrid w:linePitch=\"360\"/></w:sectPr>";

inline QString wideTable(int rows)
{
    QString grid;
    for (int c = 0; c < 6; ++c) {
        grid += QStringLiteral("<w:gridCol w:w=\"2280\"/>");
    }
    QString body;
    for (int r = 0; r < rows; ++r) {
        QString cells;
        for (int c = 0; c < 6; ++c) {
            cells += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"2280\" w:type=\"dxa\"/></w:tcPr>%1</w:tc>")
                         .arg(para(QStringLiteral("Row %1 column %2").arg(r + 1).arg(c + 1)));
        }
        body += QStringLiteral("<w:tr>%1</w:tr>").arg(cells);
    }
    return QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>"
                          "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
                          "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
                          "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
                          "</w:tblBorders></w:tblPr><w:tblGrid>%1</w:tblGrid>%2</w:tbl>")
        .arg(grid, body);
}

// Portrait (title page, roman numbers) / landscape (its own header, numbers
// restart at 1, a wide table) / portrait (two columns, its own footer,
// the header linked to the landscape one).
inline QByteArray mixedDocument()
{
    Test::DocxFixture f;
    QString body = para(QStringLiteral("Quarterly report"), QStringLiteral("<w:jc w:val=\"center\"/>"));
    for (int i = 0; i < 30; ++i) {
        body += para(QStringLiteral("Portrait paragraph %1 of the introduction, long enough to wrap across the "
                                    "text column of a portrait page with its one-inch margins.")
                         .arg(i + 1));
    }
    body += QStringLiteral("<w:p><w:pPr>%1</w:pPr><w:r><w:t>End of the introduction.</w:t></w:r></w:p>")
                .arg(QLatin1String(kSection1));
    body += para(QStringLiteral("Appendix A: the figures"));
    body += wideTable(80);
    body += QStringLiteral("<w:p><w:pPr>%1</w:pPr><w:r><w:t>End of the appendix.</w:t></w:r></w:p>")
                .arg(QLatin1String(kSection2));
    for (int i = 0; i < 20; ++i) {
        body += para(QStringLiteral("Closing paragraph %1 in two columns of the last section.").arg(i + 1));
    }
    body += QLatin1String(kSection3);
    f.body = body;
    f.header = para(QStringLiteral("Report header"));
    f.footer = QStringLiteral("<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:fldSimple w:instr=\" PAGE \">"
                              "<w:r><w:t>1</w:t></w:r></w:fldSimple></w:p>");
    addStory(f, QStringLiteral("rIdFirst"), QStringLiteral("word/header2.xml"), false, QStringLiteral("<w:p/>"));
    addStory(f, QStringLiteral("rIdWide"), QStringLiteral("word/header3.xml"), false,
             para(QStringLiteral("Landscape appendix")));
    addStory(f, QStringLiteral("rIdLast"), QStringLiteral("word/footer2.xml"), true,
             QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">Page </w:t></w:r><w:fldSimple w:instr=\" PAGE \">"
                            "<w:r><w:t>1</w:t></w:r></w:fldSimple><w:r><w:t xml:space=\"preserve\"> of </w:t></w:r>"
                            "<w:fldSimple w:instr=\" SECTIONPAGES \"><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p>"));
    return f.build();
}

} // namespace QindaOffice::Test
