// SPDX-License-Identifier: GPL-3.0-or-later
// Post-pass work on the workbook-level parts (xlsx_post_book.h, M61, D-069).

#include "xlsx_post_book.h"

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
