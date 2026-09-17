// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_master_dialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Show {

namespace {

void showColorOnButton(QPushButton *button, const QColor &color)
{
    QPixmap swatch(16, 16);
    swatch.fill(color.isValid() ? color : QColor(Qt::white));
    button->setIcon(QIcon(swatch));
    button->setText(color.isValid() ? color.name() : QObject::tr("None"));
}

} // namespace

ShowMasterDialog::ShowMasterDialog(QList<ShowModel::SlideMaster> masters, QWidget *parent)
    : QDialog(parent)
    , m_masters(std::move(masters))
{
    setObjectName(QStringLiteral("showMasterDialog"));
    setWindowTitle(tr("Slide Masters"));
    auto *layout = new QHBoxLayout(this);

    auto *left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("showMasterList"));
    m_list->setMinimumWidth(150);
    left->addWidget(m_list, 1);
    auto *add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("showMasterAdd"));
    auto *remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("showMasterRemove"));
    left->addWidget(add);
    left->addWidget(remove);
    layout->addLayout(left);

    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("showMasterName"));
    form->addRow(tr("Name:"), m_name);
    m_background = new QPushButton(this);
    m_background->setObjectName(QStringLiteral("showMasterBackground"));
    form->addRow(tr("Background:"), m_background);
    m_titleColor = new QPushButton(this);
    m_titleColor->setObjectName(QStringLiteral("showMasterTitleColor"));
    form->addRow(tr("Title colour:"), m_titleColor);
    m_bodyColor = new QPushButton(this);
    m_bodyColor->setObjectName(QStringLiteral("showMasterBodyColor"));
    form->addRow(tr("Body colour:"), m_bodyColor);
    m_font = new QFontComboBox(this);
    m_font->setObjectName(QStringLiteral("showMasterFont"));
    form->addRow(tr("Font:"), m_font);
    right->addLayout(form);
    right->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("showMasterButtons"));
    right->addWidget(buttons);
    layout->addLayout(right, 1);

    connect(add, &QPushButton::clicked, this, &ShowMasterDialog::addMaster);
    connect(remove, &QPushButton::clicked, this, &ShowMasterDialog::removeCurrent);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        // Store the row being left BEFORE moving, or edits are lost every
        // time the user clicks another master.
        storeCurrent();
        m_current = row;
        loadCurrent();
    });
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_current >= 0 && m_current < m_masters.size() && !m_loading) {
            m_masters[m_current].name = text;
            m_list->item(m_current)->setText(text);
        }
    });
    connect(m_background, &QPushButton::clicked, this, [this] {
        pickColor(&ShowModel::SlideMaster::background, m_background, tr("Background"));
    });
    connect(m_titleColor, &QPushButton::clicked, this, [this] {
        pickColor(&ShowModel::SlideMaster::titleColor, m_titleColor, tr("Title Colour"));
    });
    connect(m_bodyColor, &QPushButton::clicked, this, [this] {
        pickColor(&ShowModel::SlideMaster::bodyColor, m_bodyColor, tr("Body Colour"));
    });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        if (m_current >= 0 && m_current < m_masters.size() && !m_loading) {
            m_masters[m_current].fontFamily = font.family();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        storeCurrent();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildList();
    if (!m_masters.isEmpty()) {
        m_list->setCurrentRow(0);
    } else {
        loadCurrent();
    }
}

void ShowMasterDialog::rebuildList()
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const ShowModel::SlideMaster &master : m_masters) {
        auto *item = new QListWidgetItem(master.name.isEmpty() ? master.id : master.name, m_list);
        item->setBackground(master.background);
    }
}

void ShowMasterDialog::loadCurrent()
{
    m_loading = true;
    const bool valid = m_current >= 0 && m_current < m_masters.size();
    const QList<QWidget *> fields{m_name, m_background, m_titleColor, m_bodyColor, m_font};
    for (QWidget *widget : fields) {
        widget->setEnabled(valid);
    }
    const ShowModel::SlideMaster master =
        valid ? m_masters.at(m_current) : ShowModel::SlideMaster{};
    m_name->setText(master.name);
    showColorOnButton(m_background, master.background);
    showColorOnButton(m_titleColor, master.titleColor);
    showColorOnButton(m_bodyColor, master.bodyColor);
    m_font->setCurrentFont(QFont(master.fontFamily));
    m_loading = false;
}

void ShowMasterDialog::storeCurrent()
{
    if (m_current < 0 || m_current >= m_masters.size()) {
        return;
    }
    m_masters[m_current].name = m_name->text();
    m_masters[m_current].fontFamily = m_font->currentFont().family();
}

void ShowMasterDialog::addMaster()
{
    storeCurrent();
    ShowModel::SlideMaster master;
    master.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    master.name = tr("Master %1").arg(m_masters.size() + 1);
    m_masters.append(master);
    rebuildList();
    m_list->setCurrentRow(int(m_masters.size()) - 1);
}

void ShowMasterDialog::removeCurrent()
{
    if (m_current < 0 || m_current >= m_masters.size()) {
        return;
    }
    m_masters.removeAt(m_current);
    m_current = -1;
    rebuildList();
    m_list->setCurrentRow(m_masters.isEmpty() ? -1 : 0);
    if (m_masters.isEmpty()) {
        loadCurrent();
    }
}

void ShowMasterDialog::pickColor(QColor ShowModel::SlideMaster::*field, QPushButton *button,
                                 const QString &title)
{
    if (m_current < 0 || m_current >= m_masters.size()) {
        return;
    }
    const QColor current = m_masters.at(m_current).*field;
    const QColor picked = QColorDialog::getColor(current.isValid() ? current : QColor(Qt::white),
                                                 this, title);
    if (!picked.isValid()) {
        return;
    }
    m_masters[m_current].*field = picked;
    showColorOnButton(button, picked);
    rebuildList();
    const QSignalBlocker blocker(m_list);
    m_list->setCurrentRow(m_current);
}

} // namespace QindaOffice::Apps::Show
