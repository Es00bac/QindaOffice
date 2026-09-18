// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The window's commands (plan docs/plans/qindatk-conversion.md §3.2).
//
// AGENT-CONTRACT (AGENTS.md rule 5, moved off QAction): every command of a
// document window is one persistent ActionEntry with a stable id from
// standard_actions.h or an app-scoped literal. The id is the dbusmenu
// activation identity and the join key between the local Tk.MenuBar, the
// tool strips, the keyboard and the panel export — never rename one. The
// registry lives on the GUI thread and is owned by the DocumentHost.
//
// A radio group (`radioGroup`) keeps at most one member checked, which is
// also what the desktop's menu validator demands of an exported tree.

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Quick {

class ActionModel;

class ActionEntry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Entries are created by ActionModel::add().")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    // The text without its '&' mnemonic marker, for buttons and tooltips.
    Q_PROPERTY(QString plainText READ plainText NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY changed)
    Q_PROPERTY(QString shortcutText READ shortcutText NOTIFY changed)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(bool checkable READ isCheckable WRITE setCheckable NOTIFY changed)
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY changed)
    Q_PROPERTY(QString radioGroup READ radioGroup NOTIFY changed)
public:
    [[nodiscard]] const QString &id() const { return m_id; }
    [[nodiscard]] const QString &text() const { return m_text; }
    [[nodiscard]] QString plainText() const;
    [[nodiscard]] const QString &iconName() const { return m_iconName; }
    [[nodiscard]] const QKeySequence &shortcut() const { return m_shortcut; }
    [[nodiscard]] QString shortcutText() const;
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] bool isVisible() const { return m_visible; }
    [[nodiscard]] bool isCheckable() const { return m_checkable; }
    [[nodiscard]] bool isChecked() const { return m_checked; }
    [[nodiscard]] const QString &radioGroup() const { return m_radioGroup; }

    void setText(const QString &text);
    void setIconName(const QString &name);
    void setShortcut(const QKeySequence &shortcut);
    void setEnabled(bool enabled);
    void setVisible(bool visible);
    void setCheckable(bool checkable);
    // Checking a radio-group member unchecks the rest of its group.
    void setChecked(bool checked);
    void setRadioGroup(const QString &group);

    // Fires `triggered` (and flips `checked` for a checkable entry) when
    // enabled; a disabled command is inert from every route, including the
    // panel.
    Q_INVOKABLE void trigger();

signals:
    void changed();
    void triggered();
    // Convenience for checkable entries: the new checked state.
    void toggled(bool checked);

private:
    friend class ActionModel;
    ActionEntry(ActionModel *model, const QString &id);

    ActionModel *m_model;
    QString m_id;
    QString m_text;
    QString m_iconName;
    QKeySequence m_shortcut;
    QString m_radioGroup;
    bool m_enabled = true;
    bool m_visible = true;
    bool m_checkable = false;
    bool m_checked = false;
};

class ActionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The DocumentHost owns the window's ActionModel.")
public:
    explicit ActionModel(QObject *parent = nullptr);

    // Creates the entry (or returns the existing one for `id`). The model
    // owns it for its own lifetime.
    ActionEntry *add(const QString &id, const QString &text,
                     const QKeySequence &shortcut = {}, const QString &iconName = {});
    [[nodiscard]] Q_INVOKABLE ActionEntry *entry(const QString &id) const;
    [[nodiscard]] Q_INVOKABLE bool contains(const QString &id) const { return m_entries.contains(id); }
    [[nodiscard]] QStringList ids() const { return m_order; }
    [[nodiscard]] QList<ActionEntry *> entries() const;

    // Routes the panel's (or a test's) activation by id; false when unknown.
    Q_INVOKABLE bool trigger(const QString &id);
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void setChecked(const QString &id, bool checked);
    // Enabled for the whole list at once (a host wiring a feature set).
    void setEnabled(const QStringList &ids, bool enabled);

signals:
    // Any entry changed any field — the export re-snapshots once per
    // event-loop pass on this.
    void entryChanged(const QString &id);
    void entryAdded(const QString &id);
    void triggered(const QString &id);

private:
    friend class ActionEntry;
    void uncheckOthersInGroup(ActionEntry *keep);

    QHash<QString, ActionEntry *> m_entries;
    QStringList m_order;
};

} // namespace QindaOffice::Quick
