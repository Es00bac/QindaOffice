// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the conditional-format and validation markup (xlsx_rules.h,
// M78, D-087): Excel's names for every kind, the dxf codec, sqref text,
// and the raw XML of a worksheet's rules as read — elements with their
// character offsets in the decoded part, so a rule the model cannot hold is
// written back as the exact text it was read as (xlsx_rules_scan.cpp).

#include "sheet_rules.h"
#include "xlsx_rules.h"
#include "xlsx_style.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QXmlStreamAttributes>

#include <functional>
#include <initializer_list>
#include <optional>

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO::Rules {

// ---- namespaces and extension uris ----
inline constexpr char16_t kMainNs[] = u"http://schemas.openxmlformats.org/spreadsheetml/2006/main";
inline constexpr char16_t kRelNs[] = u"http://schemas.openxmlformats.org/officeDocument/2006/relationships";
inline constexpr char16_t kMcNs[] = u"http://schemas.openxmlformats.org/markup-compatibility/2006";
inline constexpr char16_t kX14Ns[] = u"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main";
inline constexpr char16_t kXmNs[] = u"http://schemas.microsoft.com/office/excel/2006/main";
// The worksheet extLst entries holding Excel 2010's rules, and the cfRule
// extension naming a 2007 rule's Excel 2010 twin.
inline constexpr char16_t kCfExtUri[] = u"{78C0D931-6437-407d-A8EE-F0AAD7539E65}";
inline constexpr char16_t kDvExtUri[] = u"{CCE6A557-97BC-4b89-ADB6-D9C93CAAB3DF}";
inline constexpr char16_t kRuleIdExtUri[] = u"{B025F937-C7B1-47D3-B67F-A62EFF666E3E}";

// ---- raw XML as read (xlsx_rules_scan.cpp) ----
struct RawNode {
    QString ns;          // namespace URI
    QString name;        // local name
    QString qname;       // the qualified name as written ("x14:cfRule")
    QXmlStreamAttributes attributes;
    QString text;        // character data directly inside
    QList<RawNode> children;
    qsizetype start = 0; // offset of '<' in the decoded part
    qsizetype end = 0;   // offset just past its end tag
    QHash<QString, QString> inScope;  // prefix → uri declared by its ancestors ("" = default)
    QHash<QString, QString> declared; // prefix → uri declared on it

    [[nodiscard]] const RawNode *child(QStringView localName) const;
    [[nodiscard]] QList<const RawNode *> childrenNamed(QStringView localName) const;
    // An unqualified attribute's value (null when absent).
    [[nodiscard]] QString attr(QStringView localName) const;
    [[nodiscard]] bool has(QStringView localName) const;
};

struct RuleXml {
    QString text;                          // the decoded worksheet part
    QList<RawNode> conditionalFormattings; // <conditionalFormatting>, in file order
    QList<RawNode> dataValidations;        // <dataValidation> of <dataValidations>
    QList<RawNode> x14Formattings;         // <x14:conditionalFormatting> of the extLst
    QList<RawNode> x14Validations;         // <x14:dataValidation> of the extLst
};
[[nodiscard]] RuleXml scanRuleXml(const QByteArray &sheetXml);
// The <dxf> elements of a styles part, as nodes over its decoded text.
[[nodiscard]] QList<RawNode> scanDxfs(const QByteArray &stylesXml, QString *text);

// The exact text of `node` — made to stand on its own under a new parent
// that declares `provided` (prefix → uri): a prefix it uses but its
// ancestors declared is declared on it; an Excel revision id (xr:uid) whose
// namespace is not declared there is dropped; other foreign namespaces on
// a SpreadsheetML element are also made ignorable. Nothing changes for
// markup Excel 2010 wrote, so kept rules come back byte for byte.
[[nodiscard]] QString selfContained(const QString &text, const RawNode &node, const QHash<QString, QString> &provided);
// The start tag of `node` as written.
[[nodiscard]] QString startTag(const QString &text, const RawNode &node);
// What a worksheet child, an element of our x14 ext, and one inside our
// x14:dataValidations / x14:conditionalFormatting provide.
[[nodiscard]] QHash<QString, QString> worksheetScope();
[[nodiscard]] QHash<QString, QString> x14Scope(bool withXm);

// ---- one rule into the model (xlsx_read_cfrule.cpp) ----
// How a 2007 rule's x14 twin is found: missing from the file, there to be
// merged, or in an x14 block kept as written (the 2007 rule is then kept
// too, so the two stay tied).
enum class TwinLookup { Missing, Usable, Kept };
struct RuleContext {
    const QStringList &dxfs;     // the package's dxfs (WorkbookSource::dxfs)
    const ThemePalette &palette;
    std::function<TwinLookup(const QString &id, const RawNode **twin)> twin;
};
// The node has no attribute the model would drop (Excel's revision ids aside).
[[nodiscard]] bool onlyKnownAttributes(const RawNode &node, std::initializer_list<const char *> known);
// A 2007 cfRule over `ranges`; `twin` gets the id of the x14 twin it names.
[[nodiscard]] std::optional<CondFormatRule> modelCfRule(const RuleContext &context, const RawNode &node,
                                                        const QList<CellRange> &ranges, QString *twin);
// An x14 rule with no 2007 partner (an icon set Excel 2007 lacks, a bar).
[[nodiscard]] std::optional<CondFormatRule> modelX14CfRule(const RuleContext &context, const RawNode &node,
                                                           const QList<CellRange> &ranges);

// ---- validations (xlsx_read_validation.cpp) ----
// The scan's <dataValidation> and x14 validations into `sheet`'s model,
// the rest appended to `kept`.
void readValidationRules(const RuleXml &xml, Sheet *sheet, QList<KeptRule> *kept, RuleReadCounts *counts);

// ---- writing (xlsx_write_validation.cpp) ----
// One sheet's validations: patch->dataValidationsXml and, for Excel 2010's,
// a <dataValidations> ext appended to patch->extXml.
void fillValidationPatch(const Workbook &wb, const Sheet &sheet, HiddenLists *lists,
                         PatchManifest::SheetPatch *patch, QStringList *warnings);
// A kept element whose sqref moved: the new sqref written into it (the
// attribute, or the <xm:sqref> child of an x14 element).
[[nodiscard]] QString withRanges(const QString &xml, const QList<CellRange> &ranges, bool x14);

// ---- text helpers ----
[[nodiscard]] QList<CellRange> parseSqref(const QString &text);
[[nodiscard]] QString sqrefText(const QList<CellRange> &ranges);
[[nodiscard]] QString xmlEscaped(const QString &text);   // & < > (character data)
[[nodiscard]] QString attrEscaped(const QString &text);  // & < > " and control characters
[[nodiscard]] QString rgbText(const Rgb &color);         // "FFRRGGBB"
[[nodiscard]] QString numberText(double value);          // Excel's shortest spelling
[[nodiscard]] bool boolAttr(const QString &value, bool fallback);
// The first start tag of `xml` (quoted attribute values may hold '>').
[[nodiscard]] QString firstStartTag(const QString &xml);
// Rewrites one attribute's value in the first start tag of `xml` (added
// when absent).
[[nodiscard]] QString withAttribute(const QString &xml, const QString &qname, const QString &value);

// ---- names ----
[[nodiscard]] std::optional<CondFormatRule::Op> cellIsOperator(const QString &name);
[[nodiscard]] QString cellIsOperatorName(CondFormatRule::Op op);
[[nodiscard]] std::optional<CondFormatRule::Period> timePeriodFromName(const QString &name);
[[nodiscard]] QString timePeriodName(CondFormatRule::Period period);
[[nodiscard]] std::optional<CfThreshold::Type> cfvoType(const QString &name);
[[nodiscard]] QString cfvoTypeName(CfThreshold::Type type, bool x14);
[[nodiscard]] bool isKnownIconSet(const QString &name);
[[nodiscard]] bool isX14OnlyIconSet(const QString &name);
[[nodiscard]] std::optional<ValidationRule::Kind> validationKind(const QString &type);
[[nodiscard]] QString validationTypeName(ValidationRule::Kind kind);
[[nodiscard]] std::optional<ValidationRule::Operator> validationOperator(const QString &name);
[[nodiscard]] QString validationOperatorName(ValidationRule::Operator op);

// ---- dxfs (xlsx_dxf.cpp) ----
// What the model reads from a <dxf>: the font's b, i, u, strike and colour
// and the visible fill (patternFill's bgColor, else its fgColor).
[[nodiscard]] CondFormat decodeDxf(const QString &dxfXml, const ThemePalette &palette);
// A <dxf> for `format`; the children the model does not hold (number
// format, alignment, protection, border, the font's size and name, a fill
// the model could not read) come from `base`, the dxf it was read from.
[[nodiscard]] QByteArray encodeDxf(const CondFormat &format, const QString &base, const ThemePalette &palette);

// ---- one rule's markup (xlsx_write_cfrule.cpp) ----
// A 2007 <cfRule> (with its x14:id extension when `x14Id` is set).
[[nodiscard]] QByteArray cfRuleXml(const CondFormatRule &rule, int priority, int dxfId, const QString &x14Id);
// The Excel 2010 rule: a data bar's twin (no priority) or an x14-only icon set.
[[nodiscard]] QByteArray x14RuleXml(const CondFormatRule &rule, int priority, const QString &id);
// The rule needs Excel 2010's markup: a bar that uses its settings, an icon
// set Excel 2007 lacks.
[[nodiscard]] bool needsX14Twin(const CondFormatRule &rule);
[[nodiscard]] bool isX14Only(const CondFormatRule &rule);

} // namespace QindaOffice::Calc::IO::Rules
