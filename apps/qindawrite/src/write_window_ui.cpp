// SPDX-License-Identifier: GPL-3.0-or-later
// WriteWindow's status readout and its Preferences page. Split out of
// write_window.cpp to keep it inside the line budget.
//
// The word count lives in the permanent status field rather than only
// behind a dialog: it is the number a word processor is asked for most
// often, and reading it should not take a menu trip.

#include "write_window.h"

#include "page_text_view.h"
#include "spell_checker.h"
#include "write_document.h"
#include "write_typography.h"

#include "preferences.h"
#include "preferences_dialog.h"

#include <QCheckBox>
#include <QTextDocument>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Write {

void WriteWindow::updateWriteStatus()
{
    const WriteTypography::Counts counts = m_typography->counts();
    QString info = counts.selectionOnly
        ? tr("Selected: %1 words, %2 characters").arg(counts.words).arg(counts.characters)
        : tr("%1 words   %2 characters").arg(counts.words).arg(counts.characters);
    info += tr("   Pages: %1").arg(m_view->pageCount());
    info += tr("   Zoom: %1%").arg(qRound(m_view->zoom() * 100));
    setStatusInfo(info);
}

void WriteWindow::addPreferencePages(Shell::PreferencesDialog *dialog)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_spellCheckBox = new QCheckBox(tr("Check spelling as you type"), page);
    m_spellCheckBox->setObjectName(QStringLiteral("prefSpellCheck"));
    m_spellCheckBox->setChecked(Shell::Preferences::instance().spellCheckEnabled());
    layout->addWidget(m_spellCheckBox);
    layout->addStretch(1);
    // The dialog applies its own pages on OK; this one writes through on
    // toggle, which is simpler than threading a second apply hook and keeps
    // the preference and the checkbox on one source of truth.
    connect(m_spellCheckBox, &QCheckBox::toggled, this, [](bool on) {
        Shell::Preferences::instance().setSpellCheckEnabled(on);
    });
    dialog->addPage(page, tr("Writing"));
}

void WriteWindow::applyPreferences()
{
    const Shell::Preferences &prefs = Shell::Preferences::instance();
    QTextDocument *text = m_document->textDocument();
    QFont documentFont = text->defaultFont();
    if (!prefs.documentFontFamily().isEmpty()) {
        documentFont.setFamily(prefs.documentFontFamily());
    }
    if (prefs.documentFontSize() > 0) {
        documentFont.setPointSizeF(prefs.documentFontSize());
    }
    if (documentFont != text->defaultFont()) {
        text->setDefaultFont(documentFont);
    }
    m_view->setZoom(prefs.defaultZoomPercent() / 100.0);
    if (m_spell != nullptr) {
        m_spell->setEnabled(prefs.spellCheckEnabled());
    }
    updateWriteStatus();
}

} // namespace QindaOffice::Apps::Write
