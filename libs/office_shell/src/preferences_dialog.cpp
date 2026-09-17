// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferences_dialog.h"

#include "preferences.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace QindaOffice::Shell {

PreferencesDialog::PreferencesDialog(Preferences *prefs, QWidget *parent)
    : QDialog(parent)
    , m_prefs(prefs)
    , m_tabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowTitle(tr("Preferences"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);

    buildGeneralPage();
    buildDocumentsPage();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("preferencesButtons"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreferencesDialog::buildGeneralPage()
{
    auto *page = new QWidget(m_tabs);
    auto *form = new QFormLayout(page);

    m_showToolBars = new QCheckBox(tr("Show toolbars"), page);
    m_showToolBars->setObjectName(QStringLiteral("prefShowToolBars"));
    m_showToolBars->setChecked(m_prefs->showToolBars());
    form->addRow(m_showToolBars);

    m_showStatusBar = new QCheckBox(tr("Show status bar"), page);
    m_showStatusBar->setObjectName(QStringLiteral("prefShowStatusBar"));
    m_showStatusBar->setChecked(m_prefs->showStatusBar());
    form->addRow(m_showStatusBar);

    m_defaultZoom = new QSpinBox(page);
    m_defaultZoom->setObjectName(QStringLiteral("prefDefaultZoom"));
    m_defaultZoom->setRange(Preferences::kMinZoomPercent, Preferences::kMaxZoomPercent);
    m_defaultZoom->setSingleStep(10);
    m_defaultZoom->setSuffix(tr(" %"));
    m_defaultZoom->setValue(m_prefs->defaultZoomPercent());
    form->addRow(tr("Zoom new windows to:"), m_defaultZoom);

    m_restorePrevious = new QCheckBox(tr("Reopen the previous session's documents"), page);
    m_restorePrevious->setObjectName(QStringLiteral("prefRestorePrevious"));
    form->addRow(m_restorePrevious);

    m_tabs->addTab(page, tr("General"));
}

void PreferencesDialog::buildDocumentsPage()
{
    auto *page = new QWidget(m_tabs);
    auto *form = new QFormLayout(page);

    m_autoSave = new QSpinBox(page);
    m_autoSave->setObjectName(QStringLiteral("prefAutoSave"));
    m_autoSave->setRange(0, Preferences::kMaxAutoSaveMinutes);
    m_autoSave->setSpecialValueText(tr("Off"));
    m_autoSave->setSuffix(tr(" min"));
    m_autoSave->setValue(m_prefs->autoSaveMinutes());
    form->addRow(tr("Auto-save every:"), m_autoSave);
    auto *autoSaveNote = new QLabel(
        tr("Auto-save rewrites documents that already have a file. Unsaved\n"
           "documents are protected by crash recovery, which is always on."),
        page);
    autoSaveNote->setWordWrap(true);
    form->addRow(QString(), autoSaveNote);

    m_useThemeFont = new QCheckBox(tr("Use the application's default font"), page);
    m_useThemeFont->setObjectName(QStringLiteral("prefUseThemeFont"));
    form->addRow(m_useThemeFont);

    m_fontFamily = new QFontComboBox(page);
    m_fontFamily->setObjectName(QStringLiteral("prefFontFamily"));
    form->addRow(tr("Document font:"), m_fontFamily);

    m_fontSize = new QSpinBox(page);
    m_fontSize->setObjectName(QStringLiteral("prefFontSize"));
    m_fontSize->setRange(4, 96);
    m_fontSize->setSuffix(tr(" pt"));
    form->addRow(tr("Size:"), m_fontSize);

    const QString family = m_prefs->documentFontFamily();
    const qreal size = m_prefs->documentFontSize();
    const bool themeFont = family.isEmpty() && size <= 0;
    m_useThemeFont->setChecked(themeFont);
    if (!family.isEmpty()) {
        m_fontFamily->setCurrentFont(QFont(family));
    }
    m_fontSize->setValue(size > 0 ? qRound(size) : 11);
    // The two controls only mean anything when the theme font is refused.
    const auto syncFontControls = [this](bool useTheme) {
        m_fontFamily->setEnabled(!useTheme);
        m_fontSize->setEnabled(!useTheme);
    };
    syncFontControls(themeFont);
    connect(m_useThemeFont, &QCheckBox::toggled, this, syncFontControls);

    m_tabs->addTab(page, tr("Documents"));
}

void PreferencesDialog::addPage(QWidget *page, const QString &title)
{
    m_tabs->addTab(page, title);
}

void PreferencesDialog::setRestorePrevious(bool enabled, bool available)
{
    m_restorePrevious->setChecked(enabled);
    m_restorePrevious->setEnabled(available);
}

bool PreferencesDialog::restorePrevious() const
{
    return m_restorePrevious->isChecked();
}

void PreferencesDialog::apply()
{
    m_prefs->setShowToolBars(m_showToolBars->isChecked());
    m_prefs->setShowStatusBar(m_showStatusBar->isChecked());
    m_prefs->setDefaultZoomPercent(m_defaultZoom->value());
    m_prefs->setAutoSaveMinutes(m_autoSave->value());
    if (m_useThemeFont->isChecked()) {
        m_prefs->setDocumentFontFamily(QString());
        m_prefs->setDocumentFontSize(0);
    } else {
        m_prefs->setDocumentFontFamily(m_fontFamily->currentFont().family());
        m_prefs->setDocumentFontSize(m_fontSize->value());
    }
}

} // namespace QindaOffice::Shell
