// SPDX-License-Identifier: GPL-3.0-or-later
// Post-pass work on the workbook-level parts (xlsx_post_book.h, M61, D-069).

#include "xlsx_post_book.h"

#include "xlsx_write_features.h"
#include "xlsx_xml.h"

#include "ooxml_package.h"
#include "ooxml_properties.h"

#include <QDateTime>
#include <QXmlStreamWriter>

namespace QindaOffice::Calc::IO {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

QByteArray definedNamesXml(const QList<PatchManifest::DefinedNameEntry> &names)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("definedNames"));
    for (const PatchManifest::DefinedNameEntry &entry : names) {
        writer.writeStartElement(QStringLiteral("definedName"));
        writer.writeAttribute(QStringLiteral("name"), entry.name);
        if (!entry.comment.isEmpty()) {
            writer.writeAttribute(QStringLiteral("comment"), entry.comment);
        }
        if (entry.localSheetId >= 0) {
            writer.writeAttribute(QStringLiteral("localSheetId"), QString::number(entry.localSheetId));
        }
        if (entry.hidden) {
            writer.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
        }
        writer.writeCharacters(entry.body);
        writer.writeEndElement();
    }
    writer.writeEndElement();
    return xml;
}

// xl/metadata.xml with the one block a dynamic-array cell's cm="1" names:
// metadata type XLDAPR (Excel's dynamic array properties), fDynamic="1".
// The exact block Excel writes (M66, D-075).
QByteArray dynamicArrayMetadataXml()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<metadata xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
        " xmlns:xda=\"http://schemas.microsoft.com/office/spreadsheetml/2017/dynamicarray\">"
        "<metadataTypes count=\"1\"><metadataType name=\"XLDAPR\" minSupportedVersion=\"120000\" copy=\"1\""
        " pasteAll=\"1\" pasteValues=\"1\" merge=\"1\" splitFirst=\"1\" rowColShift=\"1\" clearFormats=\"1\""
        " clearComments=\"1\" assign=\"1\" coerce=\"1\" cellMeta=\"1\"/></metadataTypes>"
        "<futureMetadata name=\"XLDAPR\" count=\"1\"><bk><extLst><ext uri=\"{bdbb8cdc-fa1e-496e-a857-3c3f30c029c3}\">"
        "<xda:dynamicArrayProperties fDynamic=\"1\" fCollapsed=\"0\"/></ext></extLst></bk></futureMetadata>"
        "<cellMetadata count=\"1\"><bk><rc t=\"1\" v=\"0\"/></bk></cellMetadata></metadata>");
}

// The M66 workbook-level edits: the structure lock, the VBA code name, the
// dynamic-array metadata and the macro-enabled content type.
bool applyWorkbookFeatures(QHash<QString, QByteArray> *parts, const PatchManifest &manifest, QString *error)
{
    // AGENT-GUARD: no reference into `parts` lives across an insert — a
    // QHash insert may rehash and leave it dangling.
    QByteArray workbookXml = parts->value(QStringLiteral("xl/workbook.xml"));
    if (const QByteArray lock = workbookProtectionXml(manifest.protection); !lock.isEmpty()
        && !insertBeforeAny(&workbookXml, {QStringLiteral("bookViews"), QStringLiteral("sheets")}, lock)) {
        return fail(error, QStringLiteral("post-pass: no workbookProtection anchor in xl/workbook.xml"));
    }
    if (!manifest.source.codeName.isEmpty()) {
        const qsizetype pr = workbookXml.indexOf("<workbookPr");
        if (pr < 0) {
            return fail(error, QStringLiteral("post-pass: no <workbookPr> in xl/workbook.xml"));
        }
        workbookXml.insert(pr + 11, QByteArrayLiteral(" codeName=\"") + xmlAttribute(manifest.source.codeName) + '"');
    }
    parts->insert(QStringLiteral("xl/workbook.xml"), workbookXml);
    QByteArray types = parts->value(QStringLiteral("[Content_Types].xml"));
    if (manifest.macroEnabled) {
        // An .xlsm's main part says so, VBA project or not (Excel checks
        // the extension against it).
        const QByteArray plain = QByteArrayLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
        if (!types.contains(plain)) {
            return fail(error, QStringLiteral("post-pass: no workbook content type to make macro-enabled"));
        }
        types.replace(plain, QByteArrayLiteral("application/vnd.ms-excel.sheet.macroEnabled.main+xml"));
    }
    if (manifest.dynamicArrays) {
        appendContentTypeOverride(&types, QStringLiteral("/xl/metadata.xml"),
                                  QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheetMetadata+xml"));
        QByteArray rels = parts->value(QStringLiteral("xl/_rels/workbook.xml.rels"));
        if (appendRelationship(&rels, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/sheetMetadata"),
                               QStringLiteral("metadata.xml"))
                .isEmpty()) {
            return fail(error, QStringLiteral("post-pass: broken rels part xl/_rels/workbook.xml.rels"));
        }
        parts->insert(QStringLiteral("xl/_rels/workbook.xml.rels"), rels);
        parts->insert(QStringLiteral("xl/metadata.xml"), dynamicArrayMetadataXml());
    }
    parts->insert(QStringLiteral("[Content_Types].xml"), types);
    return true;
}

} // namespace

bool applyWorkbookPatches(QHash<QString, QByteArray> *parts, const PatchManifest &manifest, QString *error)
{
    const QString workbookPart = QStringLiteral("xl/workbook.xml");
    if (!parts->contains(workbookPart)) {
        return fail(error, QStringLiteral("post-pass: %1 missing from the package").arg(workbookPart));
    }
    QByteArray &workbookXml = (*parts)[workbookPart];
    // Every defined name in one block after <sheets> (schema order; QXlsx
    // writes no externalReferences or functionGroups for us). QXlsx's own
    // defineName is not used: it knows neither hidden names nor comments.
    if (!manifest.definedNames.isEmpty()
        && !insertAfterTag(&workbookXml, QStringLiteral("sheets"), definedNamesXml(manifest.definedNames))) {
        return fail(error, QStringLiteral("post-pass: no </sheets> in %1").arg(workbookPart));
    }
    // Ask Excel to recalculate on open: the cached values are the engine's,
    // and a function it cannot compute was cached as #NAME?.
    const QByteArray calc = QByteArrayLiteral("<calcPr calcId=\"124519\"");
    if (!workbookXml.contains(calc)) {
        return fail(error, QStringLiteral("post-pass: no stock <calcPr> in %1").arg(workbookPart));
    }
    workbookXml.replace(calc, calc + QByteArrayLiteral(" fullCalcOnLoad=\"1\""));
    if (!applyWorkbookFeatures(parts, manifest, error)) {
        return false;
    }

    // AGENT-NOTE: QXlsx's docProps claim "Microsoft Excel" as the
    // application and "Qt Xlsx Library" as the author. They are replaced by
    // the properties the source file carried (title, author, … kept;
    // QindaCalc as the application) through office_ooxml's writer — the one
    // every Office mapper in the suite uses — built in a scratch package and
    // copied over QXlsx's parts of the same names.
    const WorkbookSource &source = manifest.source;
    Ooxml::CoreProperties core;
    core.title = source.title;
    core.subject = source.subject;
    core.creator = source.creator;
    core.keywords = source.keywords;
    core.description = source.description;
    core.category = source.category;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    core.created = source.created.isValid() ? source.created : now;
    core.modified = now;
    Ooxml::PackageWriter scratch;
    Ooxml::writeProperties(scratch, core, QStringLiteral("QindaCalc"));
    for (const QString &part : {QStringLiteral("docProps/core.xml"), QStringLiteral("docProps/app.xml")}) {
        if (!parts->contains(part)) {
            return fail(error, QStringLiteral("post-pass: %1 missing from the package").arg(part));
        }
        parts->insert(part, scratch.partData(part));
    }
    // The source's own theme, verbatim (D-068 rule 2: carry what we do not
    // model). Styles are written as resolved RGB, so cell colours do not
    // depend on it; fonts and anything else theme-driven do.
    if (!source.themeXml.isEmpty()) {
        const QString themePart = QStringLiteral("xl/theme/theme1.xml");
        if (!parts->contains(themePart)) {
            return fail(error, QStringLiteral("post-pass: %1 missing from the package").arg(themePart));
        }
        parts->insert(themePart, source.themeXml);
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
