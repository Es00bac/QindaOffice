// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Conditional formats and data validations as Excel writes them (M78,
// D-087): every rule kind of the 2007 markup and of Excel 2010's
// x14 extensions read into the model where the engine can show it — and
// the rest kept verbatim (Sheet::keptRules) so a save never loses a rule
// silently; written back in Excel's markup by the post-pass, kept rules
// byte for byte, priorities and dxfIds kept while they still hold.
//
// Reading: readSheetRules over a worksheet part's XML. Writing:
// fillRulesPatch per sheet fills PatchManifest::SheetPatch's condFormatXml,
// dataValidationsXml and extXml; one DxfTable per save gathers the dxfs
// (the source's first, unchanged, then the new ones) for
// PatchManifest::dxfsXml. xlsx_post_m78.cpp puts it all in place.

#include "xlsx_manifest.h"
#include "xlsx_style.h"
#include "xlsx_write_rules.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

// The <dxf> elements of a styles part, as written (WorkbookSource::dxfs).
[[nodiscard]] QStringList readSourceDxfs(const QByteArray &stylesXml);
// The same for a package, its styles part found through the workbook's
// relationships.
[[nodiscard]] QStringList readPackageDxfs(QXlsx::ZipReader &zip);
// The open warnings for the rules a workbook keeps as Excel wrote them
// without showing them, one per sheet that has some.
[[nodiscard]] QStringList keptRuleNotes(const Workbook &wb);

// How a sheet's rules were read: into the model, or kept as written.
struct RuleReadCounts {
    int modelledFormats = 0;
    int keptFormats = 0;
    int modelledValidations = 0;
    int keptValidations = 0;
    RuleReadCounts &operator+=(const RuleReadCounts &other)
    {
        modelledFormats += other.modelledFormats;
        keptFormats += other.keptFormats;
        modelledValidations += other.modelledValidations;
        keptValidations += other.keptValidations;
        return *this;
    }
};

// Reads a worksheet part's <conditionalFormatting> elements, its
// <dataValidations> and the x14 rules of its <extLst> into `sheet`:
// condFormats and validations where the model holds the rule, keptRules
// for the rest. `sourceDxfs` are the package's dxfs (readSourceDxfs), the
// palette resolves their theme colours.
void readSheetRules(const QByteArray &sheetXml, const QStringList &sourceDxfs, const ThemePalette &palette,
                    Sheet *sheet, RuleReadCounts *counts = nullptr);

// The dxf table of one save: the source package's dxfs first, as read
// (index i stays dxfId i), then the dxfs the new and changed formats need.
class DxfTable {
public:
    DxfTable(const QStringList &sourceDxfs, const ThemePalette &palette);
    // The dxfId a modelled rule's format is written with: its source dxf
    // when the format is still the one read from it, else a new dxf (one
    // per distinct format) — the source dxf's parts the model does not hold
    // (a number format, borders) kept in it.
    [[nodiscard]] int idFor(const CondFormatRule &rule);
    // "<dxfs count="N">…</dxfs>", or empty when the table is empty.
    [[nodiscard]] QByteArray xml() const;

private:
    QStringList m_source;
    ThemePalette m_palette;
    QList<QByteArray> m_added;
    QList<CondFormat> m_addedFormats;
};

// One sheet's conditional formats, validations and their Excel 2010
// extensions into `patch` (condFormatXml, dataValidationsXml, extXml).
// `lists` gives long validation lists a home (xlsx_write_rules.h);
// `warnings` hears about kept rules whose cells moved under them.
void fillRulesPatch(const Workbook &wb, const Sheet &sheet, DxfTable *dxfs, HiddenLists *lists,
                    PatchManifest::SheetPatch *patch, QStringList *warnings);

} // namespace QindaOffice::Calc::IO
