// SPDX-License-Identifier: GPL-3.0-or-later
// Placing the M78 parts of a sheet (xlsx_post_m78.h, D-087). Every
// insertion names the elements that FOLLOW it in the worksheet schema
// (ECMA-376 CT_Worksheet), so the order the post-pass and the carry stage
// insert things in does not matter; insertBeforeAny only ever matches the
// worksheet's own children.

#include "xlsx_post_m78.h"

#include "xlsx_xml.h"

namespace QindaOffice::Calc::IO {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

// CT_Worksheet's children from autoFilter on, in schema order.
const QStringList &followers()
{
    static const QStringList order{
        QStringLiteral("autoFilter"), QStringLiteral("sortState"), QStringLiteral("dataConsolidate"),
        QStringLiteral("customSheetViews"), QStringLiteral("mergeCells"), QStringLiteral("phoneticPr"),
        QStringLiteral("conditionalFormatting"), QStringLiteral("dataValidations"), QStringLiteral("hyperlinks"),
        QStringLiteral("printOptions"), QStringLiteral("pageMargins"), QStringLiteral("pageSetup"),
        QStringLiteral("headerFooter"), QStringLiteral("rowBreaks"), QStringLiteral("colBreaks"),
        QStringLiteral("customProperties"), QStringLiteral("cellWatches"), QStringLiteral("ignoredErrors"),
        QStringLiteral("smartTags"), QStringLiteral("drawing"), QStringLiteral("legacyDrawing"),
        QStringLiteral("legacyDrawingHF"), QStringLiteral("drawingHF"), QStringLiteral("picture"),
        QStringLiteral("oleObjects"), QStringLiteral("controls"), QStringLiteral("webPublishItems"),
        QStringLiteral("tableParts"), QStringLiteral("extLst"), QStringLiteral("/worksheet")};
    return order;
}

// The elements that follow `name` (which must be in followers(), or one of
// the print elements' first, printOptions).
QStringList after(const QString &name)
{
    return followers().mid(followers().indexOf(name) + 1);
}

// Puts `<f …/>` first in each listed cell (before its <v>).
bool addCellFormulas(QByteArray *xml, const QHash<QString, QByteArray> &formulas)
{
    qsizetype done = 0;
    for (qsizetype pos = xml->indexOf("<c r=\""); pos >= 0 && done < formulas.size(); pos = xml->indexOf("<c r=\"", pos + 1)) {
        const qsizetype refEnd = xml->indexOf('"', pos + 6);
        const auto found = formulas.constFind(QString::fromLatin1(xml->mid(pos + 6, refEnd - pos - 6)));
        if (found == formulas.constEnd()) {
            continue;
        }
        const qsizetype tagEnd = xml->indexOf('>', refEnd);
        if (tagEnd < 0) {
            return false;
        }
        if (xml->at(tagEnd - 1) == '/') {
            // <c r="B3" s="1"/> → <c r="B3" s="1"><f …/></c>
            xml->replace(tagEnd - 1, 2, QByteArrayLiteral(">") + found.value() + QByteArrayLiteral("</c>"));
        } else {
            xml->insert(tagEnd + 1, found.value());
        }
        ++done;
    }
    return done == formulas.size();
}

} // namespace

bool applyM78SheetPatch(QByteArray *sheetXml, const PatchManifest::SheetPatch &patch, QString *error)
{
    if (!patch.cellFormulaXml.isEmpty() && !addCellFormulas(sheetXml, patch.cellFormulaXml)) {
        return fail(error, QStringLiteral("post-pass: a data-table cell is missing from %1").arg(patch.sheetPart));
    }
    const QList<QPair<QString, QByteArray>> pieces{
        {QStringLiteral("scenarios"), patch.scenariosXml},
        {QStringLiteral("conditionalFormatting"), patch.condFormatXml},
        {QStringLiteral("dataValidations"), patch.dataValidationsXml},
        // printOptions … colBreaks travel as one block: the first of them
        // names the followers of the last (colBreaks).
        {QStringLiteral("colBreaks"), patch.printXml},
    };
    for (const auto &[name, xml] : pieces) {
        if (!xml.isEmpty() && !insertBeforeAny(sheetXml, name == QLatin1String("scenarios")
                                                             ? followers()
                                                             : after(name),
                                               xml)) {
            return fail(error, QStringLiteral("post-pass: no %1 anchor in %2").arg(name, patch.sheetPart));
        }
    }
    if (!patch.extXml.isEmpty()) {
        // Ours come first: nothing else has written the sheet's extLst yet
        // (the carry stage adds its entries into this one).
        const qsizetype end = topLevelElementEnd(*sheetXml, QStringLiteral("extLst"));
        if (end >= 0) {
            sheetXml->insert(end, patch.extXml);
        } else if (!insertBeforeAny(sheetXml, {QStringLiteral("/worksheet")},
                                    QByteArrayLiteral("<extLst>") + patch.extXml + QByteArrayLiteral("</extLst>"))) {
            return fail(error, QStringLiteral("post-pass: no </worksheet> in %1").arg(patch.sheetPart));
        }
    }
    return true;
}

bool applyDxfs(QByteArray *stylesXml, const QByteArray &dxfsXml)
{
    if (dxfsXml.isEmpty()) {
        return true;
    }
    const qsizetype open = topLevelTagOffset(*stylesXml, {QStringLiteral("dxfs")});
    if (open >= 0) {
        const qsizetype tagEnd = stylesXml->indexOf('>', open);
        const qsizetype close = stylesXml->at(tagEnd - 1) == '/' ? tagEnd + 1
                                                                  : stylesXml->indexOf("</dxfs>", tagEnd) + 7;
        stylesXml->replace(open, close - open, dxfsXml);
        return true;
    }
    // CT_Stylesheet: dxfs follow cellStyles and precede tableStyles, colors
    // and extLst.
    return insertBeforeAny(stylesXml,
                           {QStringLiteral("tableStyles"), QStringLiteral("colors"), QStringLiteral("extLst"),
                            QStringLiteral("/styleSheet")},
                           dxfsXml);
}

} // namespace QindaOffice::Calc::IO
