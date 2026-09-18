// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_model.h"

namespace QindaOffice::Quick {

ActionEntry::ActionEntry(ActionModel *model, const QString &id)
    : QObject(model)
    , m_model(model)
    , m_id(id)
{
}

QString ActionEntry::plainText() const
{
    QString plain;
    plain.reserve(m_text.size());
    for (qsizetype i = 0; i < m_text.size(); ++i) {
        if (m_text.at(i) == QLatin1Char('&')) {
            if (i + 1 < m_text.size() && m_text.at(i + 1) == QLatin1Char('&')) {
                plain.append(QLatin1Char('&'));
                ++i;
            }
            continue;
        }
        plain.append(m_text.at(i));
    }
    return plain;
}

QString ActionEntry::shortcutText() const
{
    return m_shortcut.toString(QKeySequence::NativeText);
}

void ActionEntry::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setIconName(const QString &name)
{
    if (m_iconName == name) {
        return;
    }
    m_iconName = name;
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setShortcut(const QKeySequence &shortcut)
{
    if (m_shortcut == shortcut) {
        return;
    }
    m_shortcut = shortcut;
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    m_visible = visible;
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setCheckable(bool checkable)
{
    if (m_checkable == checkable) {
        return;
    }
    m_checkable = checkable;
    if (!checkable && m_checked) {
        m_checked = false;
    }
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setChecked(bool checked)
{
    if (!m_checkable || m_checked == checked) {
        return;
    }
    m_checked = checked;
    if (checked && !m_radioGroup.isEmpty()) {
        // AGENT-GUARD: one checked member per group — the exported tree is
        // rejected whole by the desktop's validator otherwise.
        m_model->uncheckOthersInGroup(this);
    }
    emit changed();
    emit toggled(m_checked);
    emit m_model->entryChanged(m_id);
}

void ActionEntry::setRadioGroup(const QString &group)
{
    if (m_radioGroup == group) {
        return;
    }
    m_radioGroup = group;
    if (!group.isEmpty()) {
        m_checkable = true;
    }
    emit changed();
    emit m_model->entryChanged(m_id);
}

void ActionEntry::trigger()
{
    if (!m_enabled) {
        return;
    }
    if (m_checkable) {
        // A radio member stays checked when triggered again (a tool stays
        // the tool); a plain checkable toggles.
        setChecked(m_radioGroup.isEmpty() ? !m_checked : true);
    }
    emit triggered();
    emit m_model->triggered(m_id);
}

ActionModel::ActionModel(QObject *parent)
    : QObject(parent)
{
}

ActionEntry *ActionModel::add(const QString &id, const QString &text,
                              const QKeySequence &shortcut, const QString &iconName)
{
    if (ActionEntry *existing = m_entries.value(id)) {
        return existing;
    }
    auto *entry = new ActionEntry(this, id);
    entry->m_text = text;
    entry->m_shortcut = shortcut;
    entry->m_iconName = iconName;
    m_entries.insert(id, entry);
    m_order.append(id);
    emit entryAdded(id);
    return entry;
}

ActionEntry *ActionModel::entry(const QString &id) const
{
    return m_entries.value(id);
}

QList<ActionEntry *> ActionModel::entries() const
{
    QList<ActionEntry *> list;
    list.reserve(m_order.size());
    for (const QString &id : m_order) {
        list.append(m_entries.value(id));
    }
    return list;
}

bool ActionModel::trigger(const QString &id)
{
    ActionEntry *found = m_entries.value(id);
    if (found == nullptr) {
        return false;
    }
    found->trigger();
    return true;
}

void ActionModel::setEnabled(const QString &id, bool enabled)
{
    if (ActionEntry *found = m_entries.value(id)) {
        found->setEnabled(enabled);
    }
}

void ActionModel::setChecked(const QString &id, bool checked)
{
    if (ActionEntry *found = m_entries.value(id)) {
        found->setChecked(checked);
    }
}

void ActionModel::setEnabled(const QStringList &ids, bool enabled)
{
    for (const QString &id : ids) {
        setEnabled(id, enabled);
    }
}

void ActionModel::uncheckOthersInGroup(ActionEntry *keep)
{
    for (ActionEntry *other : std::as_const(m_entries)) {
        if (other == keep || other->m_radioGroup != keep->m_radioGroup || !other->m_checked) {
            continue;
        }
        other->m_checked = false;
        emit other->changed();
        emit other->toggled(false);
        emit entryChanged(other->m_id);
    }
}

} // namespace QindaOffice::Quick
