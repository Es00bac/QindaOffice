// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_preferences_page.h"

#include "note_window.h"

#include "preferences.h"
#include "preferences_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Notes {

void NoteWindow::addPreferencePages(Shell::PreferencesDialog *dialog)
{
    dialog->addPage(new NotePreferencesPage, tr("Notes"));
}

NotePreferencesPage::NotePreferencesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    Shell::Preferences &prefs = Shell::Preferences::instance();

    m_pressure = new QCheckBox(tr("Pen pressure changes width"), this);
    m_pressure->setObjectName(QStringLiteral("prefNotePressure"));
    m_pressure->setChecked(prefs.notePressureEnabled());
    form->addRow(m_pressure);
    connect(m_pressure, &QCheckBox::toggled, this, [](bool on) {
        Shell::Preferences::instance().setNotePressureEnabled(on);
    });

    m_smoothing = new QComboBox(this);
    m_smoothing->setObjectName(QStringLiteral("prefNoteSmoothing"));
    m_smoothing->addItem(tr("Off"), QStringLiteral("off"));
    m_smoothing->addItem(tr("Light"), QStringLiteral("light"));
    m_smoothing->addItem(tr("Strong"), QStringLiteral("strong"));
    const int current = m_smoothing->findData(prefs.noteSmoothing());
    m_smoothing->setCurrentIndex(current >= 0 ? current : 1);
    form->addRow(tr("Stroke smoothing:"), m_smoothing);
    connect(m_smoothing, &QComboBox::activated, this, [this](int index) {
        Shell::Preferences::instance().setNoteSmoothing(m_smoothing->itemData(index).toString());
    });

    layout->addLayout(form);
    layout->addStretch(1);
}

} // namespace QindaOffice::Apps::Notes
