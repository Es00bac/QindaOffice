// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_format_controller.h"

#include "spreadsheet_view.h"
#include "standard_actions.h"

#include "wrapping_tool_bar.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QIcon>
#include <QMenu>
#include <QToolBar>

namespace QindaOffice::Apps::QindaCalc {

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

const int kFontSizes[] = {6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20,
                          22, 24, 26, 28, 32, 36, 40, 44, 48, 54,
                          60, 66, 72, 80, 88, 96};

QAction *makeAction(QObject *parent, const char *id, const QString &text,
                    const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    return action;
}

} // namespace

QList<QAction *> CalcFormatActions::all() const
{
    return {bold,       italic,     underline, alignLeft, alignCenter, alignRight,
            textColor, fillColor};
}

CalcFormatActions createCalcFormatActions(QObject *parent)
{
    // Suite-standard format command ids (same conceptual commands as
    // QindaWrite's toolbar, host here is the cell style table).
    using namespace Shell::ActionId;
    CalcFormatActions a;
    a.bold = makeAction(parent, kFormatBold, QObject::tr("&Bold"), "format-text-bold");
    a.italic = makeAction(parent, kFormatItalic, QObject::tr("&Italic"), "format-text-italic");
    a.underline = makeAction(parent, kFormatUnderline, QObject::tr("&Underline"),
                             "format-text-underline");
    for (QAction *action : {a.bold, a.italic, a.underline}) {
        action->setCheckable(true);
    }
    auto *alignGroup = new QActionGroup(parent);
    alignGroup->setObjectName(QStringLiteral("calcAlignGroup"));
    alignGroup->setExclusive(true);
    a.alignLeft = makeAction(parent, kFormatAlignLeft, QObject::tr("Align &Left"),
                             "format-justify-left");
    a.alignCenter = makeAction(parent, kFormatAlignCenter, QObject::tr("Align &Center"),
                               "format-justify-center");
    a.alignRight = makeAction(parent, kFormatAlignRight, QObject::tr("Align &Right"),
                              "format-justify-right");
    for (QAction *action : {a.alignLeft, a.alignCenter, a.alignRight}) {
        action->setCheckable(true);
        alignGroup->addAction(action);
    }
    a.textColor = makeAction(parent, kFormatTextColor, QObject::tr("Text &Color…"),
                             "format-text-color");
    a.fillColor = makeAction(parent, kCalcFillColor, QObject::tr("&Fill Color…"),
                             "color-fill"); // falls back to text on lean themes
    return a;
}

CalcFormatController::CalcFormatController(SpreadsheetDocument *document,
                                           SpreadsheetView *view, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_view(view)
    , m_actions(createCalcFormatActions(this))
{
    connect(m_actions.bold, &QAction::triggered, this, [this](bool on) {
        StylePatch patch;
        patch.bold = on;
        applyPatch(patch, tr("Bold"));
    });
    connect(m_actions.italic, &QAction::triggered, this, [this](bool on) {
        StylePatch patch;
        patch.italic = on;
        applyPatch(patch, tr("Italic"));
    });
    connect(m_actions.underline, &QAction::triggered, this, [this](bool on) {
        StylePatch patch;
        patch.underline = on;
        applyPatch(patch, tr("Underline"));
    });
    connect(m_actions.alignLeft, &QAction::triggered, this, [this] {
        StylePatch patch;
        patch.alignment = 0;
        applyPatch(patch, tr("Align left"));
    });
    connect(m_actions.alignCenter, &QAction::triggered, this, [this] {
        StylePatch patch;
        patch.alignment = 1;
        applyPatch(patch, tr("Align center"));
    });
    connect(m_actions.alignRight, &QAction::triggered, this, [this] {
        StylePatch patch;
        patch.alignment = 2;
        applyPatch(patch, tr("Align right"));
    });
    connect(m_actions.textColor, &QAction::triggered, this, [this] { pickColor(false); });
    connect(m_actions.fillColor, &QAction::triggered, this, [this] { pickColor(true); });

    connect(m_view, &SpreadsheetView::cursorChanged, this,
            [this](const CalcEng::CellAddr &, const QString &, const QString &) {
                syncFromCursor();
            });
    syncFromCursor();
}

QToolBar *CalcFormatController::toolBar()
{
    if (m_toolBar != nullptr) {
        return m_toolBar;
    }
    auto *window = qobject_cast<QWidget *>(parent());
    // WrappingToolBar: the strip wraps onto another row instead of hiding
    // Align Left/Center/Right behind the extension chevron (see
    // wrapping_tool_bar.h).
    auto *bar = new Shell::WrappingToolBar(tr("Formatting"), window);
    m_toolBar = bar;
    m_toolBar->setObjectName(QStringLiteral("formatToolBar"));

    m_fontCombo = new QFontComboBox(m_toolBar);
    m_fontCombo->setObjectName(QStringLiteral("cellFontFamilyCombo"));
    m_fontCombo->setMinimumWidth(160);
    bar->addStripWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        if (m_syncing) {
            return;
        }
        StylePatch patch;
        patch.fontFamily = font.family();
        applyPatch(patch, tr("Font family"));
    });

    m_sizeCombo = new QComboBox(m_toolBar);
    m_sizeCombo->setObjectName(QStringLiteral("cellFontSizeCombo"));
    m_sizeCombo->setEditable(true);
    for (const int size : kFontSizes) {
        m_sizeCombo->addItem(QString::number(size));
    }
    bar->addStripWidget(m_sizeCombo);
    connect(m_sizeCombo, &QComboBox::activated, this, [this](int) {
        bool ok = false;
        const qreal size = m_sizeCombo->currentText().toDouble(&ok);
        if (!ok || size <= 0) {
            return;
        }
        StylePatch patch;
        patch.fontSize = size;
        applyPatch(patch, tr("Font size"));
    });

    bar->addStripSeparator();
    for (QAction *action : {m_actions.bold, m_actions.italic, m_actions.underline,
                            m_actions.textColor, m_actions.fillColor}) {
        bar->addStripAction(action);
    }
    bar->addStripSeparator();
    for (QAction *action : {m_actions.alignLeft, m_actions.alignCenter,
                            m_actions.alignRight}) {
        bar->addStripAction(action);
    }
    bar->addStripSeparator();

    m_numberCombo = new QComboBox(m_toolBar);
    m_numberCombo->setObjectName(QStringLiteral("cellNumberFormatCombo"));
    for (const NumberFormatEntry &entry : kNumberFormats) {
        m_numberCombo->addItem(tr(entry.label), QString::fromLatin1(entry.code));
    }
    bar->addStripWidget(m_numberCombo);
    connect(m_numberCombo, &QComboBox::activated, this, [this](int index) {
        StylePatch patch;
        patch.numberFormat = m_numberCombo->itemData(index).toString();
        applyPatch(patch, tr("Number format"));
    });

    syncFromCursor();
    return m_toolBar;
}

void CalcFormatController::appendStripActions(const QList<QAction *> &actions)
{
    auto *bar = qobject_cast<Shell::WrappingToolBar *>(toolBar());
    if (bar == nullptr) {
        return;
    }
    bar->addStripSeparator();
    for (QAction *action : actions) {
        bar->addStripAction(action);
    }
}

void CalcFormatController::populateFormatMenu(QMenu *menu)
{
    menu->addAction(m_actions.bold);
    menu->addAction(m_actions.italic);
    menu->addAction(m_actions.underline);
    menu->addAction(m_actions.textColor);
    menu->addAction(m_actions.fillColor);
    menu->addSeparator();
    menu->addAction(m_actions.alignLeft);
    menu->addAction(m_actions.alignCenter);
    menu->addAction(m_actions.alignRight);
}

void CalcFormatController::applyPatch(const StylePatch &patch, const QString &undoText)
{
    m_document->restyleRange(m_view->selectionRange(), patch, undoText);
    syncFromCursor();
}

void CalcFormatController::pickColor(bool fill)
{
    const QColor color = QColorDialog::getColor(Qt::black, qobject_cast<QWidget *>(parent()),
                                                fill ? tr("Fill Color") : tr("Text Color"));
    if (!color.isValid()) {
        return;
    }
    const CalcEng::Rgb rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())};
    StylePatch patch;
    if (fill) {
        patch.bg = std::optional<CalcEng::Rgb>(rgb);
        applyPatch(patch, tr("Fill color"));
    } else {
        patch.fg = std::optional<CalcEng::Rgb>(rgb);
        applyPatch(patch, tr("Text color"));
    }
}

void CalcFormatController::syncFromCursor()
{
    m_syncing = true;
    const CalcEng::CellAddr cursor = m_view->cursorCell();
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const qint32 styleId = sheet.styleIdAt(cursor);
    const CalcEng::CellStyle *style = styleId != 0 ? m_document->workbook()->style(styleId)
                                                   : nullptr;

    m_actions.bold->setChecked(style != nullptr && style->bold);
    m_actions.italic->setChecked(style != nullptr && style->italic);
    m_actions.underline->setChecked(style != nullptr && style->underline);
    const int alignment = style != nullptr ? style->alignment : 0;
    m_actions.alignLeft->setChecked(alignment == 0);
    m_actions.alignCenter->setChecked(alignment == 1);
    m_actions.alignRight->setChecked(alignment == 2);

    if (m_fontCombo != nullptr) {
        m_fontCombo->setCurrentFont(style != nullptr && !style->fontFamily.isEmpty()
                                        ? QFont(style->fontFamily)
                                        : QFont());
    }
    if (m_sizeCombo != nullptr) {
        // AGENT-GUARD: show the size the cell is actually rendered at, never
        // an empty combo. A cell with no explicit style still has a size —
        // the view's own font — and a blank box reads as "no size set",
        // which is not a state the user can act on.
        const qreal effective = style != nullptr && style->fontSize > 0
            ? style->fontSize
            : (m_view->font().pointSizeF() > 0 ? m_view->font().pointSizeF() : 10.0);
        m_sizeCombo->setCurrentText(QString::number(int(effective)));
    }
    if (m_numberCombo != nullptr && style != nullptr) {
        const int codeIndex = m_numberCombo->findData(style->numberFormat);
        m_numberCombo->setCurrentIndex(codeIndex >= 0 ? codeIndex : 0);
    } else if (m_numberCombo != nullptr) {
        m_numberCombo->setCurrentIndex(0);
    }
    m_syncing = false;
}

} // namespace QindaOffice::Apps::QindaCalc
