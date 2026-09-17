// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote's Preferences page (M22, plan section 3.9): pen pressure and
// stroke smoothing. The two notebook defaults (background, page size)
// join this page in M25.
//
// Stock Qt widgets only (rule 3). Write-through on change, the same seam
// QindaWrite's spell-check page uses: the dialog applies its own pages on
// OK, and an app page that writes through keeps the preference and the
// control on one source of truth (Preferences::changed() re-applies every
// open window).

#include <QWidget>

class QCheckBox;
class QComboBox;

namespace QindaOffice::Apps::Notes {

class NotePreferencesPage final : public QWidget {
    Q_OBJECT
public:
    explicit NotePreferencesPage(QWidget *parent = nullptr);

private:
    QCheckBox *m_pressure = nullptr;
    QComboBox *m_smoothing = nullptr;
};

} // namespace QindaOffice::Apps::Notes
