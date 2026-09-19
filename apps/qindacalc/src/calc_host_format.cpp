// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's cell-format commands and the format strip's combos: one
// ActionEntry per command (bold, italic, underline, alignment, colours,
// the #,##0.00 shortcut) and the font family / size / number format the
// strip's Tk.ComboBoxes read and write through the host. Every check mark
// and combo re-reads the cursor cell's style, so they can never drift
// from the document.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "sheet.h"

#include <QFontDatabase>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

struct NumberFormatEntry {
    const char *label;
    const char *code;
};

// Codes follow Excel syntax (the engine's NumberFormat understands them).
const NumberFormatEntry kNumberFormats[] = {
    {QT_TR_NOOP("General"), "General"},
    {QT_TR_NOOP("Number: 0.00"), "0.00"},
    {QT_TR_NOOP("Number: #,##0.00"), "#,##0.00"},
    {QT_TR_NOOP("Percent: 0.00%"), "0.00%"},
    {QT_TR_NOOP("Currency: $#,##0.00"), "$#,##0.00"},
    {QT_TR_NOOP("Date: yyyy-mm-dd"), "yyyy-mm-dd"},
};

const int kFontSizes[] = {6,  7,  8,  9,  10, 11, 12, 14, 16, 18, 20, 22, 24, 26,
                          28, 32, 36, 40, 44, 48, 54, 60, 66, 72, 80, 88, 96};

} // namespace

void CalcHost::createFormatActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [model](const char *id, const QString &text, const char *icon) {
        return model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
    };
    const auto checkable = [this](ActionEntry *entry, auto patchFor, const QString &undoText) {
        entry->setCheckable(true);
        connect(entry, &ActionEntry::toggled, this, [this, patchFor, undoText](bool on) {
            if (!m_syncing) {
                applyPatch(patchFor(on), undoText);
            }
        });
    };
    checkable(make(ActionId::kFormatBold, tr("&Bold"), "bold"),
              [](bool on) { StylePatch p; p.bold = on; return p; }, tr("Bold"));
    checkable(make(ActionId::kFormatItalic, tr("&Italic"), "italic"),
              [](bool on) { StylePatch p; p.italic = on; return p; }, tr("Italic"));
    checkable(make(ActionId::kFormatUnderline, tr("&Underline"), "underline"),
              [](bool on) { StylePatch p; p.underline = on; return p; }, tr("Underline"));
    checkable(make(ActionId::kFormatStrikeOut, tr("&Strikethrough"), "strikethrough"),
              [](bool on) { StylePatch p; p.strikeout = on; return p; }, tr("Strikethrough"));
    const struct {
        const char *id;
        const char *text;
        const char *icon;
        int alignment;
    } aligns[] = {{ActionId::kFormatAlignLeft, QT_TR_NOOP("Align &Left"), "align-left", 0},
                  {ActionId::kFormatAlignCenter, QT_TR_NOOP("Align &Center"), "align-center", 1},
                  {ActionId::kFormatAlignRight, QT_TR_NOOP("Align &Right"), "align-right", 2}};
    for (const auto &align : aligns) {
        ActionEntry *entry = make(align.id, tr(align.text), align.icon);
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("calcAlign"));
        connect(entry, &ActionEntry::triggered, this, [this, alignment = align.alignment] {
            if (!m_syncing) {
                StylePatch patch;
                patch.alignment = alignment;
                applyPatch(patch, tr("Alignment"));
            }
        });
    }
    // The colour commands open the strip's swatch popovers; from the menu
    // or the panel they ask the window for a picker (colourPicker seams).
    make(ActionId::kFormatTextColor, tr("Text &Colour…"), "baseline");
    make(ActionId::kCalcFillColor, tr("&Fill Colour…"), "paint-bucket");
    connect(model->add(QStringLiteral("calcThousandsFormatAction"), tr("Number format: #,##0.00")),
            &ActionEntry::triggered, this, [this] {
                StylePatch patch;
                patch.numberFormat = QStringLiteral("#,##0.00");
                const CellAddr at = cursor();
                m_document->restyleRange({at, at}, patch, tr("Number format"));
                refreshGrid();
            });
}

QStringList CalcHost::fontFamilies() const
{
    return QFontDatabase::families();
}

QStringList CalcHost::fontSizes() const
{
    QStringList sizes;
    for (const int size : kFontSizes) {
        sizes.append(QString::number(size));
    }
    return sizes;
}

QVariantList CalcHost::numberFormats() const
{
    QVariantList list;
    for (const NumberFormatEntry &entry : kNumberFormats) {
        list.append(QVariantMap{{QStringLiteral("label"), tr(entry.label)},
                                {QStringLiteral("code"), QString::fromLatin1(entry.code)}});
    }
    return list;
}

QString CalcHost::cellFontFamily() const
{
    const CellStyle *style = m_document->workbook()->style(sheet().styleIdAt(cursor()));
    if (style != nullptr && !style->fontFamily.isEmpty()) {
        return style->fontFamily;
    }
    return m_grid != nullptr ? m_grid->cellFont().family() : QString();
}

QString CalcHost::cellFontSize() const
{
    // AGENT-GUARD: show the size the cell is actually rendered at, never
    // an empty combo. A cell with no explicit style still has a size — the
    // grid's own font.
    const CellStyle *style = m_document->workbook()->style(sheet().styleIdAt(cursor()));
    const qreal effective = style != nullptr && style->fontSize > 0
        ? style->fontSize
        : (m_grid != nullptr && m_grid->cellFont().pointSizeF() > 0 ? m_grid->cellFont().pointSizeF() : 10.0);
    return QString::number(int(effective));
}

int CalcHost::cellNumberFormatIndex() const
{
    const CellStyle *style = m_document->workbook()->style(sheet().styleIdAt(cursor()));
    if (style == nullptr) {
        return 0;
    }
    int index = 0;
    for (const NumberFormatEntry &entry : kNumberFormats) {
        if (style->numberFormat == QLatin1String(entry.code)) {
            return index;
        }
        ++index;
    }
    return 0;
}

void CalcHost::applyPatch(const StylePatch &patch, const QString &undoText)
{
    m_document->restyleRange(selection(), patch, undoText);
    refreshGrid();
    syncFormatFromCursor();
    emit cursorChanged();
}

void CalcHost::applyFontFamily(const QString &family)
{
    if (family.isEmpty() || family == cellFontFamily()) {
        return;
    }
    StylePatch patch;
    patch.fontFamily = family;
    applyPatch(patch, tr("Font family"));
}

void CalcHost::applyFontSize(const QString &text)
{
    bool ok = false;
    const qreal size = text.trimmed().toDouble(&ok);
    if (!ok || size <= 0 || size > 400) {
        return;
    }
    StylePatch patch;
    patch.fontSize = size;
    applyPatch(patch, tr("Font size"));
}

void CalcHost::applyNumberFormat(int index)
{
    if (index < 0 || index >= int(std::size(kNumberFormats))) {
        return;
    }
    StylePatch patch;
    patch.numberFormat = QString::fromLatin1(kNumberFormats[index].code);
    applyPatch(patch, tr("Number format"));
}

void CalcHost::applyTextColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    StylePatch patch;
    patch.fg = std::optional<Rgb>(Rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())});
    applyPatch(patch, tr("Text colour"));
}

void CalcHost::applyFillColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    StylePatch patch;
    patch.bg = std::optional<Rgb>(Rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())});
    applyPatch(patch, tr("Fill colour"));
}

void CalcHost::syncFormatFromCursor()
{
    m_syncing = true;
    const CellStyle *style = m_document->workbook()->style(sheet().styleIdAt(cursor()));
    Quick::ActionModel *model = actions();
    model->setChecked(QString::fromLatin1(ActionId::kFormatBold), style != nullptr && style->bold);
    model->setChecked(QString::fromLatin1(ActionId::kFormatItalic), style != nullptr && style->italic);
    model->setChecked(QString::fromLatin1(ActionId::kFormatUnderline), style != nullptr && style->underline);
    model->setChecked(QString::fromLatin1(ActionId::kFormatStrikeOut), style != nullptr && style->strikeout);
    const int alignment = style != nullptr ? style->alignment : 0;
    model->setChecked(QString::fromLatin1(ActionId::kFormatAlignLeft), alignment == 0);
    model->setChecked(QString::fromLatin1(ActionId::kFormatAlignCenter), alignment == 1);
    model->setChecked(QString::fromLatin1(ActionId::kFormatAlignRight), alignment == 2);
    m_syncing = false;
}

} // namespace QindaOffice::Apps::QindaCalc
