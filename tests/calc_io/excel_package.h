// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel-shaped packages built by hand for the M61 gates (D-069). The parts,
// relationships and cell spellings are the ones Microsoft Excel itself
// writes: numbers with no t attribute, shared strings with rich and
// phonetic runs, inline strings, t="e"/"b"/"str"/"d" cells, _xlfn.-prefixed
// functions, scoped defined names, a theme and document properties. The
// fixtures live in code, so no binary file — and none of the owner's
// documents — ever enters the repository.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace QindaOffice::Test {

struct ExcelSheet {
    QString name;
    QByteArray sheetData;  // the <row> elements, verbatim
    QByteArray afterData;  // elements after </sheetData> (mergeCells, dataValidations, …)
    QString state;         // "" (visible) or "hidden"
};

struct ExcelWorkbook {
    QList<ExcelSheet> sheets;
    QList<QByteArray> sharedStrings; // the inner XML of each <si>
    QByteArray definedNames;         // <definedName> elements, verbatim
    QByteArray themeXml;             // empty: no theme part
    QByteArray coreXml;              // empty: no docProps/core.xml
    QByteArray stylesXml;            // empty: kDefaultStyles below

    // Two cell formats, as Excel numbers them: xf 0 is General, xf 1 is the
    // built-in short date (numFmtId 14).
    static QByteArray defaultStyles()
    {
        return QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
            "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
            "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
            "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
            "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
            "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
            "<xf numFmtId=\"14\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/></cellXfs>"
            "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
            "</styleSheet>");
    }

    [[nodiscard]] QByteArray build() const
    {
        using namespace Ooxml;
        const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
        const QString ct = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.");
        const QString workbookPart = QStringLiteral("xl/workbook.xml");
        PackageWriter writer;
        writer.addRelationship(QString(), QString::fromUtf16(RelType::OfficeDocument), workbookPart);

        QByteArray sheetsXml;
        for (int i = 0; i < sheets.size(); ++i) {
            const ExcelSheet &sheet = sheets.at(i);
            const QString part = QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1);
            const QString rid = writer.addRelationship(workbookPart, rel + QStringLiteral("worksheet"), part);
            writer.addPart(part,
                           QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                             "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                                             "<sheetData>")
                               + sheet.sheetData + QByteArrayLiteral("</sheetData>") + sheet.afterData
                               + QByteArrayLiteral("</worksheet>"),
                           ct + QStringLiteral("worksheet+xml"));
            sheetsXml += QByteArrayLiteral("<sheet name=\"") + sheet.name.toUtf8().replace('&', "&amp;")
                + QByteArrayLiteral("\" sheetId=\"") + QByteArray::number(i + 1) + '"'
                + (sheet.state.isEmpty() ? QByteArray() : QByteArrayLiteral(" state=\"") + sheet.state.toUtf8() + '"')
                + QByteArrayLiteral(" r:id=\"") + rid.toUtf8() + QByteArrayLiteral("\"/>");
        }
        writer.addPart(QStringLiteral("xl/styles.xml"), stylesXml.isEmpty() ? defaultStyles() : stylesXml,
                       ct + QStringLiteral("styles+xml"));
        writer.addRelationship(workbookPart, rel + QStringLiteral("styles"), QStringLiteral("xl/styles.xml"));
        if (!sharedStrings.isEmpty()) {
            QByteArray sst = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                               "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"")
                + QByteArray::number(sharedStrings.size()) + QByteArrayLiteral("\" uniqueCount=\"")
                + QByteArray::number(sharedStrings.size()) + QByteArrayLiteral("\">");
            for (const QByteArray &si : sharedStrings) {
                sst += QByteArrayLiteral("<si>") + si + QByteArrayLiteral("</si>");
            }
            sst += QByteArrayLiteral("</sst>");
            writer.addPart(QStringLiteral("xl/sharedStrings.xml"), sst, ct + QStringLiteral("sharedStrings+xml"));
            writer.addRelationship(workbookPart, rel + QStringLiteral("sharedStrings"),
                                   QStringLiteral("xl/sharedStrings.xml"));
        }
        if (!themeXml.isEmpty()) {
            writer.addPart(QStringLiteral("xl/theme/theme1.xml"), themeXml, QString::fromUtf16(ContentType::Theme));
            writer.addRelationship(workbookPart, QString::fromUtf16(RelType::Theme), QStringLiteral("xl/theme/theme1.xml"));
        }
        if (!coreXml.isEmpty()) {
            writer.addPart(QStringLiteral("docProps/core.xml"), coreXml,
                           QString::fromUtf16(ContentType::CoreProperties));
            writer.addRelationship(QString(), QString::fromUtf16(RelType::CoreProperties),
                                   QStringLiteral("docProps/core.xml"));
        }
        writer.addPart(workbookPart,
                       QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                         "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                                         "<workbookPr/><bookViews><workbookView/></bookViews><sheets>")
                           + sheetsXml + QByteArrayLiteral("</sheets>")
                           + (definedNames.isEmpty() ? QByteArray()
                                                     : QByteArrayLiteral("<definedNames>") + definedNames
                                                           + QByteArrayLiteral("</definedNames>"))
                           + QByteArrayLiteral("<calcPr calcId=\"191029\"/></workbook>"),
                       ct + QStringLiteral("sheet.main+xml"));
        return writer.toZip();
    }
};

} // namespace QindaOffice::Test
