// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sections (M75, D-084), as PowerPoint's Home ▸ Section: Add Section (at
// the current slide), Rename Section…, Remove Section, Remove All Sections,
// Move Section Up / Down, Collapse All / Expand All. The slide sorter
// shows a header per section (name, slide count) that collapses its slides
// (SlideSorter.qml); collapsing is a view state, not saved. Each change is
// ONE undo step (ShowHost::applyDeckChange); the sections are written as
// PowerPoint's p14:sectionLst (show_model's sections.h, pptx_review.h).
//
// AGENT-CONTRACT: the command ids (SectionActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5): never renamed, never reused.

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QSet>
#include <QVariantList>

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace SectionActionId {
inline const char kAdd[] = "showAddSectionAction";
inline const char kRename[] = "showRenameSectionAction";
inline const char kRemove[] = "showRemoveSectionAction";
inline const char kRemoveAll[] = "showRemoveAllSectionsAction";
inline const char kMoveUp[] = "showMoveSectionUpAction";
inline const char kMoveDown[] = "showMoveSectionDownAction";
inline const char kCollapseAll[] = "showCollapseSectionsAction";
inline const char kExpandAll[] = "showExpandSectionsAction";
} // namespace SectionActionId

class ShowSections final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // One row per slide and section header, in sorter order:
    // [{header: true, section, name, count, collapsed, first (its first
    // slide's row, -1 when empty)} | {header: false, slide (row), section}]
    // — the slides of a collapsed section left out.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(bool any READ any NOTIFY changed)
public:
    explicit ShowSections(ShowHost *host);

    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] bool any() const;
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    // The section of the current slide (-1 without sections).
    [[nodiscard]] int currentSection() const;

    Q_INVOKABLE void toggleCollapsed(int section);
    Q_INVOKABLE void rename(int section, const QString &name);
    Q_INVOKABLE void addSection(const QString &name = QString());
    Q_INVOKABLE void removeSection(int section);
    Q_INVOKABLE void moveSection(int section, int delta);

signals:
    void changed();
    // Rename Section… asks for a name (the current one given).
    void renameRequested(int section, const QString &current);

private:
    void sync();

    ShowHost *m_host;
    QSet<QString> m_collapsed; // section ids
    QHash<QString, Quick::ActionEntry *> m_actions;
};

} // namespace QindaOffice::Apps::Show
