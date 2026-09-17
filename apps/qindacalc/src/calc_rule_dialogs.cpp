// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_rule_dialogs.h"

#include "value.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace QindaOffice::Apps::QindaCalc {

namespace {

using Op = CalcEng::CondFormatRule::Op;
using Kind = CalcEng::ValidationRule::Kind;

QString rangeText(const CalcEng::CellRange &range)
{
    if (!range.isValid()) {
        return {};
    }
    if (range.topLeft == range.bottomRight) {
        return CalcEng::addrText(range.topLeft);
    }
    return QStringLiteral("%1:%2").arg(CalcEng::addrText(range.topLeft),
                                       CalcEng::addrText(range.bottomRight));
}

QColor toQColor(const std::optional<CalcEng::Rgb> &rgb)
{
    return rgb.has_value() ? QColor(rgb->r, rgb->g, rgb->b) : QColor();
}

std::optional<CalcEng::Rgb> toRgb(const QColor &color)
{
    if (!color.isValid()) {
        return std::nullopt;
    }
    return CalcEng::Rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())};
}

// Shows the chosen colour on the button itself, without a stylesheet
// (AGENTS.md rule 3: no QSS): a small filled pixmap as the button's icon.
void showColorOnButton(QPushButton *button, const QColor &color, const QString &noneText)
{
    if (!color.isValid()) {
        button->setIcon(QIcon());
        button->setText(noneText);
        return;
    }
    QPixmap swatch(16, 16);
    swatch.fill(color);
    button->setIcon(QIcon(swatch));
    button->setText(color.name());
}

} // namespace

CondFormatDialog::CondFormatDialog(QList<CalcEng::CondFormatRule> rules,
                                   const CalcEng::CellRange &selection, QWidget *parent)
    : QDialog(parent)
    , m_rules(std::move(rules))
    , m_selection(selection)
{
    setObjectName(QStringLiteral("condFormatDialog"));
    setWindowTitle(tr("Conditional Formatting"));
    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("condFormatList"));
    layout->addWidget(new QLabel(tr("Rules on this sheet:"), this));
    layout->addWidget(m_list, 1);

    auto *remove = new QPushButton(tr("Remove Rule"), this);
    remove->setObjectName(QStringLiteral("condFormatRemove"));
    layout->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, &CondFormatDialog::removeSelected);

    auto *box = new QGroupBox(tr("New rule for %1").arg(rangeText(m_selection)), this);
    auto *form = new QFormLayout(box);
    m_op = new QComboBox(box);
    m_op->setObjectName(QStringLiteral("condFormatOp"));
    m_op->addItem(tr("Greater than"), int(Op::GreaterThan));
    m_op->addItem(tr("Less than"), int(Op::LessThan));
    m_op->addItem(tr("Between"), int(Op::Between));
    m_op->addItem(tr("Equal to"), int(Op::EqualTo));
    m_op->addItem(tr("Not equal to"), int(Op::NotEqualTo));
    m_op->addItem(tr("Text contains"), int(Op::ContainsText));
    m_op->addItem(tr("Is empty"), int(Op::IsEmpty));
    m_op->addItem(tr("Is not empty"), int(Op::NotEmpty));
    form->addRow(tr("Condition:"), m_op);

    m_first = new QDoubleSpinBox(box);
    m_first->setObjectName(QStringLiteral("condFormatFirst"));
    m_first->setRange(-1e12, 1e12);
    m_first->setDecimals(4);
    form->addRow(tr("Value:"), m_first);
    m_second = new QDoubleSpinBox(box);
    m_second->setObjectName(QStringLiteral("condFormatSecond"));
    m_second->setRange(-1e12, 1e12);
    m_second->setDecimals(4);
    form->addRow(tr("And:"), m_second);
    m_text = new QLineEdit(box);
    m_text->setObjectName(QStringLiteral("condFormatText"));
    form->addRow(tr("Text:"), m_text);

    m_background = new QPushButton(tr("None"), box);
    m_background->setObjectName(QStringLiteral("condFormatBackground"));
    form->addRow(tr("Fill:"), m_background);
    m_foreground = new QPushButton(tr("None"), box);
    m_foreground->setObjectName(QStringLiteral("condFormatForeground"));
    form->addRow(tr("Text colour:"), m_foreground);
    m_bold = new QCheckBox(tr("Bold"), box);
    m_bold->setObjectName(QStringLiteral("condFormatBold"));
    m_italic = new QCheckBox(tr("Italic"), box);
    m_italic->setObjectName(QStringLiteral("condFormatItalic"));
    auto *flags = new QHBoxLayout;
    flags->addWidget(m_bold);
    flags->addWidget(m_italic);
    form->addRow(QString(), flags);

    auto *add = new QPushButton(tr("Add Rule"), box);
    add->setObjectName(QStringLiteral("condFormatAdd"));
    form->addRow(add);
    layout->addWidget(box);

    connect(m_background, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(
            m_backgroundColor.isValid() ? m_backgroundColor : QColor(Qt::yellow), this,
            tr("Fill Colour"));
        if (picked.isValid()) {
            m_backgroundColor = picked;
            showColorOnButton(m_background, picked, tr("None"));
        }
    });
    connect(m_foreground, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(
            m_foregroundColor.isValid() ? m_foregroundColor : QColor(Qt::red), this,
            tr("Text Colour"));
        if (picked.isValid()) {
            m_foregroundColor = picked;
            showColorOnButton(m_foreground, picked, tr("None"));
        }
    });
    connect(add, &QPushButton::clicked, this, &CondFormatDialog::addRule);
    // The value fields that mean nothing for the chosen condition are
    // disabled rather than hidden, so the dialog does not jump about.
    const auto syncFields = [this] {
        const Op op = Op(m_op->currentData().toInt());
        const bool numeric = op == Op::GreaterThan || op == Op::LessThan || op == Op::Between
            || op == Op::EqualTo || op == Op::NotEqualTo;
        m_first->setEnabled(numeric);
        m_second->setEnabled(op == Op::Between);
        m_text->setEnabled(op == Op::ContainsText || op == Op::EqualTo || op == Op::NotEqualTo);
    };
    connect(m_op, &QComboBox::currentIndexChanged, this, syncFields);
    syncFields();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("condFormatButtons"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuildList();
}

QString CondFormatDialog::describe(const CalcEng::CondFormatRule &rule) const
{
    QString condition;
    switch (rule.op) {
    case Op::GreaterThan:
        condition = tr("> %1").arg(rule.first);
        break;
    case Op::LessThan:
        condition = tr("< %1").arg(rule.first);
        break;
    case Op::Between:
        condition = tr("between %1 and %2").arg(rule.first).arg(rule.second);
        break;
    case Op::EqualTo:
        condition = tr("= %1").arg(rule.text.isEmpty() ? QString::number(rule.first) : rule.text);
        break;
    case Op::NotEqualTo:
        condition =
            tr("≠ %1").arg(rule.text.isEmpty() ? QString::number(rule.first) : rule.text);
        break;
    case Op::ContainsText:
        condition = tr("contains “%1”").arg(rule.text);
        break;
    case Op::IsEmpty:
        condition = tr("is empty");
        break;
    case Op::NotEmpty:
        condition = tr("is not empty");
        break;
    }
    return tr("%1  %2").arg(rangeText(rule.range), condition);
}

void CondFormatDialog::rebuildList()
{
    m_list->clear();
    for (const CalcEng::CondFormatRule &rule : m_rules) {
        auto *item = new QListWidgetItem(describe(rule), m_list);
        if (rule.format.background) {
            item->setBackground(toQColor(rule.format.background));
        }
        if (rule.format.foreground) {
            item->setForeground(toQColor(rule.format.foreground));
        }
    }
}

void CondFormatDialog::addRule()
{
    if (!m_selection.isValid()) {
        return;
    }
    CalcEng::CondFormatRule rule;
    rule.range = m_selection;
    rule.op = Op(m_op->currentData().toInt());
    rule.first = m_first->value();
    rule.second = m_second->value();
    rule.text = m_text->text();
    rule.format.background = toRgb(m_backgroundColor);
    rule.format.foreground = toRgb(m_foregroundColor);
    if (m_bold->isChecked()) {
        rule.format.bold = true;
    }
    if (m_italic->isChecked()) {
        rule.format.italic = true;
    }
    m_rules.append(rule);
    rebuildList();
}

void CondFormatDialog::removeSelected()
{
    const int row = m_list->currentRow();
    if (row >= 0 && row < m_rules.size()) {
        m_rules.removeAt(row);
        rebuildList();
    }
}

ValidationDialog::ValidationDialog(const CalcEng::ValidationRule *existing,
                                   const CalcEng::CellRange &selection, QWidget *parent)
    : QDialog(parent)
    , m_selection(selection)
{
    setObjectName(QStringLiteral("validationDialog"));
    setWindowTitle(tr("Data Validation"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addWidget(new QLabel(tr("Applies to %1").arg(rangeText(m_selection)), this));

    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("validationKind"));
    m_kind->addItem(tr("Any value (remove validation)"), -1);
    m_kind->addItem(tr("Whole number"), int(Kind::WholeNumber));
    m_kind->addItem(tr("Decimal"), int(Kind::Decimal));
    m_kind->addItem(tr("List"), int(Kind::List));
    m_kind->addItem(tr("Text length"), int(Kind::TextLength));
    m_kind->addItem(tr("Not empty"), int(Kind::NotEmpty));
    form->addRow(tr("Allow:"), m_kind);

    m_min = new QDoubleSpinBox(this);
    m_min->setObjectName(QStringLiteral("validationMin"));
    m_min->setRange(-1e12, 1e12);
    m_min->setDecimals(4);
    form->addRow(tr("Minimum:"), m_min);
    m_max = new QDoubleSpinBox(this);
    m_max->setObjectName(QStringLiteral("validationMax"));
    m_max->setRange(-1e12, 1e12);
    m_max->setDecimals(4);
    m_max->setValue(100);
    form->addRow(tr("Maximum:"), m_max);
    m_list = new QLineEdit(this);
    m_list->setObjectName(QStringLiteral("validationList"));
    m_list->setPlaceholderText(tr("Comma-separated values"));
    form->addRow(tr("Entries:"), m_list);
    m_message = new QLineEdit(this);
    m_message->setObjectName(QStringLiteral("validationMessage"));
    m_message->setPlaceholderText(tr("Shown when input is refused"));
    form->addRow(tr("Message:"), m_message);
    layout->addLayout(form);

    if (existing != nullptr) {
        m_kind->setCurrentIndex(m_kind->findData(int(existing->kind)));
        m_min->setValue(existing->min);
        m_max->setValue(existing->max);
        m_list->setText(existing->list.join(QStringLiteral(", ")));
        m_message->setText(existing->message);
    }
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ValidationDialog::syncControls);
    syncControls();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("validationButtons"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_clear = m_kind->currentData().toInt() < 0;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ValidationDialog::syncControls()
{
    const int kind = m_kind->currentData().toInt();
    const bool ranged = kind == int(Kind::WholeNumber) || kind == int(Kind::Decimal)
        || kind == int(Kind::TextLength);
    m_min->setEnabled(ranged);
    m_max->setEnabled(ranged);
    m_list->setEnabled(kind == int(Kind::List));
    m_message->setEnabled(kind >= 0);
}

CalcEng::ValidationRule ValidationDialog::rule() const
{
    CalcEng::ValidationRule out;
    out.range = m_selection;
    out.kind = Kind(qMax(0, m_kind->currentData().toInt()));
    out.min = m_min->value();
    out.max = m_max->value();
    for (const QString &entry : m_list->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        out.list.append(entry.trimmed());
    }
    out.message = m_message->text();
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
