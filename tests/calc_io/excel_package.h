// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel-shaped packages built by hand for the M61 gates (D-069). The parts,
// relationships and cell spellings are the ones Microsoft Excel itself
// writes: numbers with no t attribute, shared strings with rich and
// phonetic runs, inline strings, t="e"/"b"/"str"/"d" cells, _xlfn.-prefixed
// functions, scoped defined names, a theme and document properties. The
// fixtures live in code, so no binary file — and none of the owner's
// documents — ever enters the repository.
//
// M66 (D-075) adds what the Excel features around the cells need: a
// sheet's own relationships and the elements before <sheetData> (sheetPr,
// sheetFormatPr, cols), extra parts with their relationships (pivot tables,
// slicers, the VBA project, …), and workbook-level elements in their schema
// slots (workbookProtection, externalReferences, pivotCaches, extLst).

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace QindaOffice::Test {

// A relationship as the builder takes it: internal targets are PART NAMES
// ("xl/pivotTables/pivotTable1.xml"), made relative on output; external
// targets are written verbatim with TargetMode="External".
struct ExcelRel {
    QString id;
    QString type; // the full relationship URI
    QString target;
    bool external = false;
};

// Any further part, with its own relationships.
struct ExcelPart {
    QString name;
    QByteArray data;
    QString contentType; // empty: declared by extension (xml, rels, images)
    QList<ExcelRel> rels;
};

inline QString officeRel(const char *name)
{
    return QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/")
        + QString::fromLatin1(name);
}

inline QString smlType(const char *name)
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.")
        + QString::fromLatin1(name);
}

struct ExcelSheet {
    // The older tests brace-initialise the first four fields; a
    // constructor keeps that working as the struct grows.
    ExcelSheet(QString sheetName = {}, QByteArray data = {}, QByteArray after = {}, QString sheetState = {})
        : name(std::move(sheetName))
        , sheetData(std::move(data))
        , afterData(std::move(after))
        , state(std::move(sheetState))
    {
    }

    QString name;
    QByteArray sheetData;  // the <row> elements, verbatim
    QByteArray afterData;  // elements after </sheetData> (mergeCells, dataValidations, …)
    QString state;         // "" (visible) or "hidden"
    QByteArray beforeData; // M66: sheetPr … cols, verbatim, before <sheetData>
    QList<ExcelRel> rels;  // M66: the sheet part's relationships (fixed ids)
    QByteArray rootAttributes; // M66: extra declarations on <worksheet> (mc, x14ac, xr, …)
    // M69 (D-078): a chart sheet — the whole xl/chartsheets/sheet<N>.xml
    // part, verbatim (its drawing hangs from `rels`); the worksheet fields
    // above are unused.
    QByteArray chartSheetXml;
};

struct ExcelWorkbook {
    QList<ExcelSheet> sheets;
    QList<QByteArray> sharedStrings; // the inner XML of each <si>
    QByteArray definedNames;         // <definedName> elements, verbatim
    QByteArray themeXml;             // empty: no theme part
    QByteArray coreXml;              // empty: no docProps/core.xml
    QByteArray stylesXml;            // empty: kDefaultStyles below
    // M66 (D-075): workbook-level elements in their schema slots, the
    // workbook part's and the package's extra relationships, extra parts,
    // and the main part's content type (macro-enabled workbooks differ).
    QByteArray workbookPr = QByteArrayLiteral("<workbookPr/>");
    QByteArray beforeBookViews; // workbookProtection
    QByteArray afterSheets;     // externalReferences (before definedNames)
    QByteArray afterCalcPr;     // pivotCaches … extLst
    QByteArray workbookRootAttributes;
    QList<ExcelRel> workbookRels; // fixed ids, after the generated ones
    QList<ExcelRel> packageRels;
    QList<ExcelPart> parts;
    QString mainContentType; // empty: sheet.main+xml

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
        const auto addRels = [&writer](const QString &source, const QList<ExcelRel> &rels) {
            for (const ExcelRel &r : rels) {
                writer.addRelationshipWithId(source, r.id, r.type, r.target, r.external);
            }
        };

        QByteArray sheetsXml;
        for (int i = 0; i < sheets.size(); ++i) {
            const ExcelSheet &sheet = sheets.at(i);
            const bool chart = !sheet.chartSheetXml.isEmpty();
            const QString kind = chart ? QStringLiteral("chartsheet") : QStringLiteral("worksheet");
            const QString part = QStringLiteral("xl/%1s/sheet%2.xml").arg(kind).arg(i + 1);
            const QString rid = QStringLiteral("rIdSheet%1").arg(i + 1);
            writer.addRelationshipWithId(workbookPart, rid, rel + kind, part);
            addRels(part, sheet.rels);
            QByteArray data = sheet.chartSheetXml;
            if (!chart) {
                data = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                         "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"")
                    + sheet.rootAttributes + QByteArrayLiteral(">") + sheet.beforeData
                    + QByteArrayLiteral("<sheetData>") + sheet.sheetData + QByteArrayLiteral("</sheetData>")
                    + sheet.afterData + QByteArrayLiteral("</worksheet>");
            }
            writer.addPart(part, data, ct + kind + QStringLiteral("+xml"));
            sheetsXml += QByteArrayLiteral("<sheet name=\"") + sheet.name.toUtf8().replace('&', "&amp;")
                + QByteArrayLiteral("\" sheetId=\"") + QByteArray::number(i + 1) + '"'
                + (sheet.state.isEmpty() ? QByteArray() : QByteArrayLiteral(" state=\"") + sheet.state.toUtf8() + '"')
                + QByteArrayLiteral(" r:id=\"") + rid.toUtf8() + QByteArrayLiteral("\"/>");
        }
        writer.addPart(QStringLiteral("xl/styles.xml"), stylesXml.isEmpty() ? defaultStyles() : stylesXml,
                       ct + QStringLiteral("styles+xml"));
        writer.addRelationshipWithId(workbookPart, QStringLiteral("rIdStyles"), rel + QStringLiteral("styles"),
                                     QStringLiteral("xl/styles.xml"));
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
            writer.addRelationshipWithId(workbookPart, QStringLiteral("rIdStrings"), rel + QStringLiteral("sharedStrings"),
                                         QStringLiteral("xl/sharedStrings.xml"));
        }
        if (!themeXml.isEmpty()) {
            writer.addPart(QStringLiteral("xl/theme/theme1.xml"), themeXml, QString::fromUtf16(ContentType::Theme));
            writer.addRelationshipWithId(workbookPart, QStringLiteral("rIdTheme"), QString::fromUtf16(RelType::Theme),
                                         QStringLiteral("xl/theme/theme1.xml"));
        }
        if (!coreXml.isEmpty()) {
            writer.addPart(QStringLiteral("docProps/core.xml"), coreXml,
                           QString::fromUtf16(ContentType::CoreProperties));
            writer.addRelationship(QString(), QString::fromUtf16(RelType::CoreProperties),
                                   QStringLiteral("docProps/core.xml"));
        }
        addRels(workbookPart, workbookRels);
        addRels(QString(), packageRels);
        for (const ExcelPart &extra : parts) {
            writer.addPart(extra.name, extra.data, extra.contentType);
            addRels(extra.name, extra.rels);
        }
        writer.addPart(workbookPart,
                       QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                         "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"")
                           + workbookRootAttributes + QByteArrayLiteral(">") + workbookPr + beforeBookViews
                           + QByteArrayLiteral("<bookViews><workbookView/></bookViews><sheets>")
                           + sheetsXml + QByteArrayLiteral("</sheets>") + afterSheets
                           + (definedNames.isEmpty() ? QByteArray()
                                                     : QByteArrayLiteral("<definedNames>") + definedNames
                                                           + QByteArrayLiteral("</definedNames>"))
                           + QByteArrayLiteral("<calcPr calcId=\"191029\"/>") + afterCalcPr
                           + QByteArrayLiteral("</workbook>"),
                       mainContentType.isEmpty() ? ct + QStringLiteral("sheet.main+xml") : mainContentType);
        return writer.toZip();
    }
};

} // namespace QindaOffice::Test
