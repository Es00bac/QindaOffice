// SPDX-License-Identifier: GPL-3.0-or-later
// Sections (show_sections.h).

#include "show_sections.h"

#include "action_model.h"
#include "menu_tree.h"
#include "sections.h"
#include "show_document.h"
#include "show_host.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace SectionId = SectionActionId;

ShowSections::ShowSections(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    Quick::ActionModel *model = host->actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        m_actions.insert(QString::fromLatin1(id), entry);
        return entry;
    };
    connect(make(SectionId::kAdd, tr("&Add Section"), "folder-plus"), &ActionEntry::triggered, this, [this] { addSection(); });
    connect(make(SectionId::kRename, tr("&Rename Section…"), "pencil"), &ActionEntry::triggered, this, [this] {
        const int section = currentSection();
        const auto &sections = m_host->showDocument()->presentation()->sections;
        if (section >= 0 && section < sections.size()) {
            emit renameRequested(section, sections.at(section).name);
        }
    });
    connect(make(SectionId::kRemove, tr("Re&move Section"), "list-minus"), &ActionEntry::triggered, this,
            [this] { removeSection(currentSection()); });
    connect(make(SectionId::kRemoveAll, tr("Remove A&ll Sections"), "x"), &ActionEntry::triggered, this, [this] {
        ShowModel::Presentation after = *m_host->showDocument()->presentation();
        ShowModel::removeAllSections(after);
        m_host->applyDeckChange(after, tr("Remove all sections"));
    });
    connect(make(SectionId::kMoveUp, tr("Move Section &Up"), "arrow-up"), &ActionEntry::triggered, this,
            [this] { moveSection(currentSection(), -1); });
    connect(make(SectionId::kMoveDown, tr("Move Section &Down"), "arrow-down"), &ActionEntry::triggered, this,
            [this] { moveSection(currentSection(), 1); });
    connect(make(SectionId::kCollapseAll, tr("&Collapse All"), "chevrons-down-up"), &ActionEntry::triggered, this, [this] {
        for (const ShowModel::SlideSection &section : m_host->showDocument()->presentation()->sections) {
            m_collapsed.insert(section.id);
        }
        emit changed();
    });
    connect(make(SectionId::kExpandAll, tr("&Expand All"), "chevrons-up-down"), &ActionEntry::triggered, this, [this] {
        m_collapsed.clear();
        emit changed();
    });
    connect(host, &ShowHost::deckChanged, this, [this] { sync(); });
    connect(host, &ShowHost::currentSlideChanged, this, [this] { sync(); });
    sync();
}

QList<MenuNode> ShowSections::menuItems() const
{
    QList<MenuNode> items;
    for (const char *id : {SectionId::kAdd, SectionId::kRename, SectionId::kRemove, SectionId::kRemoveAll, SectionId::kMoveUp,
                           SectionId::kMoveDown, SectionId::kCollapseAll, SectionId::kExpandAll}) {
        items.append(MenuNode::action(QString::fromLatin1(id)));
    }
    items.insert(4, MenuNode::separator());
    items.insert(7, MenuNode::separator());
    return {MenuNode::submenu(QStringLiteral("showSectionMenu"), tr("S&ection"), items)};
}

bool ShowSections::any() const
{
    return !m_host->showDocument()->presentation()->sections.isEmpty();
}

int ShowSections::currentSection() const
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide != nullptr ? ShowModel::sectionOf(*m_host->showDocument()->presentation(), slide->id) : -1;
}

QVariantList ShowSections::rows() const
{
    QVariantList rows;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    QHash<QString, int> rowOf;
    for (qsizetype i = 0; i < deck->slides.size(); ++i) {
        rowOf.insert(deck->slides.at(i).id, int(i));
    }
    if (deck->sections.isEmpty()) {
        for (qsizetype i = 0; i < deck->slides.size(); ++i) {
            rows.append(QVariantMap{{QStringLiteral("header"), false}, {QStringLiteral("slide"), int(i)}, {QStringLiteral("section"), -1}});
        }
        return rows;
    }
    for (qsizetype s = 0; s < deck->sections.size(); ++s) {
        const ShowModel::SlideSection &section = deck->sections.at(s);
        const bool collapsed = m_collapsed.contains(section.id);
        rows.append(QVariantMap{{QStringLiteral("header"), true},
                                {QStringLiteral("section"), int(s)},
                                {QStringLiteral("name"), section.name},
                                {QStringLiteral("count"), int(section.slides.size())},
                                {QStringLiteral("collapsed"), collapsed},
                                {QStringLiteral("first"), section.slides.isEmpty() ? -1 : rowOf.value(section.slides.first(), -1)}});
        if (collapsed) {
            continue;
        }
        for (const QString &id : section.slides) {
            if (rowOf.contains(id)) {
                rows.append(QVariantMap{{QStringLiteral("header"), false}, {QStringLiteral("slide"), rowOf.value(id)},
                                        {QStringLiteral("section"), int(s)}});
            }
        }
    }
    return rows;
}

void ShowSections::toggleCollapsed(int section)
{
    const auto &sections = m_host->showDocument()->presentation()->sections;
    if (section < 0 || section >= sections.size()) {
        return;
    }
    const QString id = sections.at(section).id;
    if (!m_collapsed.remove(id)) {
        m_collapsed.insert(id);
    }
    emit changed();
}

void ShowSections::rename(int section, const QString &name)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (ShowModel::renameSection(after, section, name)) {
        m_host->applyDeckChange(after, tr("Rename section"));
    }
}

void ShowSections::addSection(const QString &name)
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    if (slide == nullptr) {
        return;
    }
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    const int added = ShowModel::addSection(after, slide->id, name.isEmpty() ? tr("Untitled Section") : name);
    if (m_host->applyDeckChange(after, tr("Add section")) && added >= 0) {
        emit renameRequested(added, after.sections.at(added).name); // PowerPoint asks for its name
    }
}

void ShowSections::removeSection(int section)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (ShowModel::removeSection(after, section)) {
        m_host->applyDeckChange(after, tr("Remove section"));
    }
}

void ShowSections::moveSection(int section, int delta)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    const QString current = m_host->currentSlide() != nullptr ? m_host->currentSlide()->id : QString();
    if (!ShowModel::moveSection(after, section, delta)) {
        return;
    }
    m_host->applyDeckChange(after, tr("Move section"));
    // The slide the user was on stays current where it moved to.
    const auto &slides = m_host->showDocument()->presentation()->slides;
    for (qsizetype i = 0; i < slides.size(); ++i) {
        if (slides.at(i).id == current) {
            m_host->selectSlideRow(int(i));
        }
    }
}

void ShowSections::sync()
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const bool any = !deck->sections.isEmpty();
    const int section = currentSection();
    m_actions.value(QString::fromLatin1(SectionId::kAdd))->setEnabled(!deck->slides.isEmpty());
    for (const char *id : {SectionId::kRename, SectionId::kRemove}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(section >= 0);
    }
    for (const char *id : {SectionId::kRemoveAll, SectionId::kCollapseAll, SectionId::kExpandAll}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(any);
    }
    m_actions.value(QString::fromLatin1(SectionId::kMoveUp))->setEnabled(section > 0);
    m_actions.value(QString::fromLatin1(SectionId::kMoveDown))->setEnabled(section >= 0 && section < deck->sections.size() - 1);
    emit changed();
}

} // namespace QindaOffice::Apps::Show
