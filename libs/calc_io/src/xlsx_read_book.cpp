// SPDX-License-Identifier: GPL-3.0-or-later
// Workbook-level package reads (M61, D-069; print names since M37): the
// defined names of xl/workbook.xml, the print areas and titles among them,
// the source's theme part and document properties, and the late resolution
// of validation lists that point at a range.

#include "xlsx_read_xml.h"

#include "xlsx_formula.h"
#include "xlsx_parts.h"

#include "ooxml_namespaces.h"
#include "ooxml_xml.h"

#include "workbook.h"

#include <QDir>
#include <QXmlStreamReader>

#include <optional>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

bool isPrintName(const QString &name)
{
    return name.compare(QLatin1String("_xlnm.Print_Area"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("_xlnm.Print_Titles"), Qt::CaseInsensitive) == 0;
}

// The comma-separated parts of a name body, commas inside quotes kept
// ("'A, B'!$1:$1,'A, B'!$A:$A").
QStringList bodyParts(const QString &body)
{
    QStringList parts;
    QString current;
    bool quoted = false;
    for (const QChar c : body) {
        if (c == QLatin1Char('\'')) {
            quoted = !quoted; // a doubled '' toggles twice: still inside
        }
        if (c == QLatin1Char(',') && !quoted) {
            parts.append(current);
            current.clear();
            continue;
        }
        current += c;
    }
    parts.append(current);
    return parts;
}

// The ranges a print name's body names, when the model can hold them all
// (M78, D-087): each part a reference — a print area's ranges (whole rows
// and columns are ranges too), print titles' whole rows or columns (or a
// range spanning the sheet's width or height, as other producers write
// them). nullopt for anything else (a dynamic print area, =OFFSET(…), a
// name): such a name stays a DefinedName and is written back as read.
std::optional<QList<RangeRef>> mappedPrintName(const QString &name, const QString &body)
{
    const bool area = name.compare(QLatin1String("_xlnm.Print_Area"), Qt::CaseInsensitive) == 0;
    QList<RangeRef> refs;
    for (const QString &part : bodyParts(body)) {
        const ParseResult parsed = parseFormula(formulaFromFile(part.trimmed()));
        if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
            return std::nullopt;
        }
        const RangeRef &ref = parsed.root->ref;
        const bool rows = ref.wholeRows || (ref.range.topLeft.col == 0 && ref.range.bottomRight.col >= Sheet::kMaxCols - 1);
        const bool cols = ref.wholeCols || (ref.range.topLeft.row == 0 && ref.range.bottomRight.row >= Sheet::kMaxRows - 1);
        if (!ref.range.isValid() || (!area && !rows && !cols)) {
            return std::nullopt;
        }
        refs.append(ref);
    }
    if (refs.isEmpty()) {
        return std::nullopt;
    }
    return refs;
}

QString childText(const Ooxml::XmlElement &root, const char16_t *ns, QStringView name)
{
    const Ooxml::XmlElement *e = root.child(QString::fromUtf16(ns), name);
    return e != nullptr ? e->text().trimmed() : QString();
}

QString relsPathFor(const QString &part)
{
    const int slash = int(part.lastIndexOf(QLatin1Char('/')));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash) + QStringLiteral(".rels");
}

} // namespace

void readDefinedNames(const QByteArray &workbookXml, Workbook *wb)
{
    QList<DefinedName> defined;
    QXmlStreamReader reader(workbookXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement || reader.name() != QLatin1String("definedName")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QString name = attributes.value(QLatin1String("name")).toString();
        const QString scope = attributes.value(QLatin1String("localSheetId")).toString();
        const QStringView hiddenFlag = attributes.value(QLatin1String("hidden"));
        const bool hidden = hiddenFlag == QLatin1String("1") || hiddenFlag == QLatin1String("true");
        const QString comment = attributes.value(QLatin1String("comment")).toString();
        const QString text = reader.readElementText().trimmed();
        const QString body = formulaFromFile(text);
        if (name.isEmpty() || body.isEmpty() || (isPrintName(name) && mappedPrintName(name, text).has_value())) {
            continue; // print areas and titles the model holds are per-sheet fields (readPrintNames)
        }
        const ParseResult parsed = parseFormula(body);
        const bool parses = parsed.error.isEmpty() && parsed.root;
        // The engine's classic table holds what it always held: workbook-
        // wide names of a plain sheet-qualified range. Everything else —
        // sheet scope, constants, formulas, hidden or commented names,
        // Excel's other built-ins — is a DefinedName, evaluated where it
        // parses and written back either way.
        if (scope.isEmpty() && !hidden && comment.isEmpty() && parses && parsed.root->kind == Node::Kind::Ref
            && !parsed.root->ref.sheet.isEmpty() && !name.startsWith(QLatin1String("_xlnm."), Qt::CaseInsensitive)) {
            wb->defineName(name, parsed.root->ref);
            continue;
        }
        bool numbered = false;
        const int sheet = scope.toInt(&numbered);
        defined.append({name, numbered ? qint32(sheet) : -1, body, parses ? parsed.root : NodePtr(), hidden, comment});
    }
    if (!defined.isEmpty()) {
        wb->setDefinedNames(defined);
    }
}

// _xlnm built-in names (Print_Area / Print_Titles) are SHEET-LOCAL fields,
// not user names — they ride localSheetId and never enter a name table.
// Titles may combine columns and rows ("'Q1 Sales'!$A:$A,'Q1 Sales'!$1:$1"),
// and a quoted sheet name may hold any punctuation, so each part goes
// through the formula parser rather than string surgery. M78 (D-087): every
// area of a multi-area print area is kept (Sheet::printArea, then
// PageSetup::morePrintAreas); a print name the model cannot hold stays a
// sheet-scoped DefinedName (readDefinedNames).
void readPrintNames(const QByteArray &workbookXml, Workbook *wb)
{
    QXmlStreamReader reader(workbookXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement || reader.name() != QLatin1String("definedName")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QString name = attributes.value(QLatin1String("name")).toString();
        const QString scope = attributes.value(QLatin1String("localSheetId")).toString();
        const QString body = reader.readElementText().trimmed();
        bool numbered = false;
        const int sheetId = scope.toInt(&numbered);
        if (!isPrintName(name) || !numbered || sheetId < 0 || sheetId >= wb->sheetCount()) {
            continue;
        }
        const std::optional<QList<RangeRef>> refs = mappedPrintName(name, body);
        if (!refs.has_value()) {
            continue;
        }
        Sheet &sheet = wb->sheetAt(sheetId);
        if (name.compare(QLatin1String("_xlnm.Print_Area"), Qt::CaseInsensitive) == 0) {
            Sheet::PageSetup setup = sheet.pageSetup();
            setup.morePrintAreas.clear();
            for (qsizetype i = 1; i < refs->size(); ++i) {
                setup.morePrintAreas.append(refs->at(i).range);
            }
            sheet.setPrintArea(refs->first().range); // clears the further areas: set them after
            sheet.setPageSetup(setup);
            continue;
        }
        for (const RangeRef &ref : *refs) {
            const bool rows = ref.wholeRows || (ref.range.topLeft.col == 0 && ref.range.bottomRight.col >= Sheet::kMaxCols - 1);
            if (rows) {
                sheet.setPrintTitles(CellRange{{ref.range.topLeft.row, 0}, {ref.range.bottomRight.row, Sheet::kMaxCols - 1}},
                                     sheet.printTitleCols());
            } else {
                sheet.setPrintTitles(sheet.printTitleRows(),
                                     CellRange{{0, ref.range.topLeft.col}, {Sheet::kMaxRows - 1, ref.range.bottomRight.col}});
            }
        }
    }
}

WorkbookSource readWorkbookSource(QXlsx::ZipReader &zip)
{
    WorkbookSource source;
    for (const auto &[type, target] : readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"))) {
        if (!type.endsWith(QLatin1String("/theme"))) {
            continue;
        }
        const QString part = resolveTarget(QStringLiteral("xl"), target);
        // AGENT-GUARD: a theme with relationships of its own (image fills)
        // would arrive without the parts it points at; such a theme stays
        // behind and the stock one is written instead.
        if (zip.fileData(relsPathFor(part)).contains("<Relationship ")) {
            break;
        }
        source.themeXml = zip.fileData(part);
        break;
    }
    QString corePart = QStringLiteral("docProps/core.xml");
    for (const auto &[type, target] : readRels(zip, QStringLiteral("_rels/.rels"))) {
        if (type.endsWith(QLatin1String("/metadata/core-properties"))) {
            corePart = QDir::cleanPath(target.startsWith(QLatin1Char('/')) ? target.mid(1) : target);
        }
    }
    const Ooxml::XmlElementPtr core = Ooxml::parseXml(zip.fileData(corePart));
    if (core) {
        using namespace Ooxml;
        source.title = childText(*core, Ns::DC, u"title");
        source.subject = childText(*core, Ns::DC, u"subject");
        source.creator = childText(*core, Ns::DC, u"creator");
        source.keywords = childText(*core, Ns::CP, u"keywords");
        source.description = childText(*core, Ns::DC, u"description");
        source.category = childText(*core, Ns::CP, u"category");
        source.created = QDateTime::fromString(childText(*core, Ns::DCTERMS, u"created"), Qt::ISODate);
    }
    return source;
}

void resolveValidationLists(Workbook *wb)
{
    for (qint32 s = 0; s < wb->sheetCount(); ++s) {
        QList<ValidationRule> rules = wb->sheetAt(s).validations();
        bool changed = false;
        for (ValidationRule &rule : rules) {
            if (rule.kind != ValidationRule::Kind::List || rule.listSource.isEmpty()) {
                continue;
            }
            // A range, or a name standing for one; the items are its cells'
            // text in order (the engine checks entries against these values;
            // the file keeps the live reference).
            const ParseResult parsed = parseFormula(rule.listSource);
            std::optional<RangeRef> ref;
            if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
                ref = parsed.root->ref;
            } else if (parsed.root && parsed.root->kind == Node::Kind::Name) {
                ref = wb->namedRange(parsed.root->text);
            }
            if (!ref.has_value()) {
                continue;
            }
            const qint32 sheet = ref->sheet.isEmpty() ? s : wb->sheetIndex(unquotedSheetName(ref->sheet));
            if (sheet < 0 || ref->range.cellCount() > 32768) {
                continue;
            }
            rule.list.clear();
            for (qint32 row = ref->range.topLeft.row; row <= ref->range.bottomRight.row; ++row) {
                for (qint32 col = ref->range.topLeft.col; col <= ref->range.bottomRight.col; ++col) {
                    const Value value = wb->cellValue(sheet, row, col);
                    if (!isBlank(value)) {
                        rule.list.append(toText(value));
                    }
                }
            }
            changed = true;
        }
        if (changed) {
            wb->sheetAt(s).setValidations(rules);
        }
    }
}

} // namespace QindaOffice::Calc::IO
