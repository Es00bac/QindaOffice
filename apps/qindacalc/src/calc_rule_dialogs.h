// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The two rule editors QindaCalc needs a dialog for: conditional
// formatting and data validation. Stock Qt widgets only (AGENTS.md rule 3).
//
// Both edit the rules of ONE sheet and hand the whole list back, so the
// document can push a single undo command for whatever the user did in the
// dialog — adding three rules and removing one is one action to them.

#include "sheet_rules.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class CondFormatDialog final : public QDialog {
    Q_OBJECT
public:
    CondFormatDialog(QList<CalcEng::CondFormatRule> rules, const CalcEng::CellRange &selection,
                     QWidget *parent = nullptr);

    [[nodiscard]] QList<CalcEng::CondFormatRule> rules() const { return m_rules; }

private:
    void rebuildList();
    void addRule();
    void removeSelected();
    [[nodiscard]] QString describe(const CalcEng::CondFormatRule &rule) const;

    QList<CalcEng::CondFormatRule> m_rules;
    CalcEng::CellRange m_selection;
    QListWidget *m_list = nullptr;
    QComboBox *m_op = nullptr;
    QDoubleSpinBox *m_first = nullptr;
    QDoubleSpinBox *m_second = nullptr;
    QLineEdit *m_text = nullptr;
    QPushButton *m_background = nullptr;
    QPushButton *m_foreground = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_italic = nullptr;
    QColor m_backgroundColor;
    QColor m_foregroundColor;
};

class ValidationDialog final : public QDialog {
    Q_OBJECT
public:
    ValidationDialog(const CalcEng::ValidationRule *existing, const CalcEng::CellRange &selection,
                     QWidget *parent = nullptr);

    // True when the user chose "no validation" — the caller removes any
    // rule covering the selection instead of adding one.
    [[nodiscard]] bool clearsRule() const { return m_clear; }
    [[nodiscard]] CalcEng::ValidationRule rule() const;

private:
    void syncControls();

    CalcEng::CellRange m_selection;
    bool m_clear = false;
    QComboBox *m_kind = nullptr;
    QDoubleSpinBox *m_min = nullptr;
    QDoubleSpinBox *m_max = nullptr;
    QLineEdit *m_list = nullptr;
    QLineEdit *m_message = nullptr;
};

} // namespace QindaOffice::Apps::QindaCalc
