// SPDX-License-Identifier: GPL-3.0-or-later
// BaseHost (M86, D-094): the commands of Access's next tools — the query
// wizards, union queries, the Indexes dialog, data macros, hyperlinks,
// Access's XML import, linked tables and the application log — with their
// menus and enablement. The M49–M80 commands are base_host_actions.cpp.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon (BaseRibbonLayout.qml) and the
// keyboard — never rename or reuse one; AccessToolsCommands.qml answers
// each. An id appears in the menu tree exactly once.
#include "base_host.h"

#include <QGuiApplication>
#include <QKeySequence>

namespace QindaOffice::Apps::Base {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *icon;
};

constexpr Command kAccessCommands[] = {
    // Create ▸ Queries: Access's query wizards
    {"baseSimpleQueryWizardAction", QT_TRANSLATE_NOOP("BaseHost", "&Simple Query Wizard…"), "wand-sparkles"},
    {"baseCrosstabWizardAction", QT_TRANSLATE_NOOP("BaseHost", "Cross&tab Query Wizard…"), "grid-3x3"},
    {"baseFindDuplicatesWizardAction", QT_TRANSLATE_NOOP("BaseHost", "Find &Duplicates Query Wizard…"), "copy"},
    {"baseFindUnmatchedWizardAction", QT_TRANSLATE_NOOP("BaseHost", "Find &Unmatched Query Wizard…"), "unlink"},
    // Table Design: Indexes and the table events
    {"baseDesignIndexesAction", QT_TRANSLATE_NOOP("BaseHost", "&Indexes…"), "list-ordered"},
    {"baseDataMacroBeforeChangeAction", QT_TRANSLATE_NOOP("BaseHost", "Before &Change"), "zap"},
    {"baseDataMacroBeforeDeleteAction", QT_TRANSLATE_NOOP("BaseHost", "Before De&lete"), "circle-x"},
    {"baseDataMacroAfterInsertAction", QT_TRANSLATE_NOOP("BaseHost", "After &Insert"), "file-plus"},
    {"baseDataMacroAfterUpdateAction", QT_TRANSLATE_NOOP("BaseHost", "After &Update"), "pen-line"},
    {"baseDataMacroAfterDeleteAction", QT_TRANSLATE_NOOP("BaseHost", "After De&lete"), "trash-2"},
    // Records: hyperlink fields
    {"baseEditHyperlinkAction", QT_TRANSLATE_NOOP("BaseHost", "Edit &Hyperlink…"), "link"},
    {"baseOpenHyperlinkAction", QT_TRANSLATE_NOOP("BaseHost", "Open Hyper&link"), "globe"},
    // External Data: XML import and linked tables
    {"baseImportXmlAction", QT_TRANSLATE_NOOP("BaseHost", "Import &XML…"), "file-braces"},
    {"baseLinkTableAction", QT_TRANSLATE_NOOP("BaseHost", "&Link Table…"), "link-2"},
    {"baseLinkedTableManagerAction", QT_TRANSLATE_NOOP("BaseHost", "Linked Table &Manager…"), "layers-2"},
    {"baseRefreshLinksAction", QT_TRANSLATE_NOOP("BaseHost", "Refresh All Lin&ks"), "rotate-cw"},
    // Database Tools: what LogEvent wrote
    {"baseApplicationLogAction", QT_TRANSLATE_NOOP("BaseHost", "Application &Log"), "history"},
};

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

MenuNode a(const char *raw)
{
    return MenuNode::action(id(raw));
}

} // namespace

void BaseHost::createAccessToolActions()
{
    for (const Command &c : kAccessCommands) {
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(), id(c.icon));
        connect(entry, &ActionEntry::triggered, this, [this, raw = c.id] { emit actionRequested(id(raw)); });
    }
    // A union is one of the query types (the designer's radio group, M80).
    ActionEntry *unionType = actions()->add(id("baseQueryTypeUnionAction"), tr("U&nion Query"), QKeySequence(),
                                            id("combine"));
    unionType->setCheckable(true);
    unionType->setRadioGroup(QStringLiteral("baseQueryType"));
    connect(unionType, &ActionEntry::triggered, this, [this] { emit actionRequested(id("baseQueryTypeUnionAction")); });
}

void BaseHost::insertAccessToolMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString create = QStringLiteral("baseCreateMenu");
    for (const char *raw : {"baseSimpleQueryWizardAction", "baseCrosstabWizardAction", "baseFindDuplicatesWizardAction",
                            "baseFindUnmatchedWizardAction"}) {
        tree->insertInto(create, id("baseNewFormAction"), a(raw));
    }
    tree->insertInto(create, id("baseNewFormAction"), MenuNode::separator());
    const QString design = QStringLiteral("baseDesignMenu");
    tree->insertInto(design, id("baseQueryClearAction"), a("baseQueryTypeUnionAction"));
    for (const char *raw : {"baseDesignIndexesAction", "baseDataMacroBeforeChangeAction", "baseDataMacroBeforeDeleteAction",
                            "baseDataMacroAfterInsertAction", "baseDataMacroAfterUpdateAction",
                            "baseDataMacroAfterDeleteAction"}) {
        tree->insertInto(design, id("baseDesignApplyAction"), a(raw));
    }
    const QString records = QStringLiteral("baseRecordsMenu");
    tree->appendTo(records, MenuNode::separator());
    tree->appendTo(records, a("baseEditHyperlinkAction"));
    tree->appendTo(records, a("baseOpenHyperlinkAction"));
    const QString external = QStringLiteral("baseExternalMenu");
    tree->insertInto(external, id("baseImportAccessAction"), a("baseImportXmlAction"));
    tree->appendTo(external, MenuNode::separator());
    for (const char *raw : {"baseLinkTableAction", "baseLinkedTableManagerAction", "baseRefreshLinksAction"}) {
        tree->appendTo(external, a(raw));
    }
    const QString tools = QStringLiteral("baseToolsMenu");
    tree->appendTo(tools, MenuNode::separator());
    tree->appendTo(tools, a("baseApplicationLogAction"));
}

void BaseHost::syncAccessToolEnablement(bool writable)
{
    const bool db = m_service != nullptr && m_service->hasDatabase();
    for (const Command &c : kAccessCommands) {
        actions()->setEnabled(id(c.id), db);
    }
    for (const char *raw : {"baseSimpleQueryWizardAction", "baseCrosstabWizardAction", "baseFindDuplicatesWizardAction",
                            "baseFindUnmatchedWizardAction", "baseImportXmlAction", "baseLinkTableAction",
                            "baseRefreshLinksAction"}) {
        actions()->setEnabled(id(raw), writable);
    }
    actions()->setEnabled(id("baseQueryTypeUnionAction"), db && writable && m_view == QLatin1String("queryDesign"));
    // The table's own design: Indexes for any design, its events once it is saved.
    const bool tableDesign = db && writable && m_view == QLatin1String("tableDesign");
    actions()->setEnabled(id("baseDesignIndexesAction"), tableDesign);
    for (const char *raw : {"baseDataMacroBeforeChangeAction", "baseDataMacroBeforeDeleteAction",
                            "baseDataMacroAfterInsertAction", "baseDataMacroAfterUpdateAction",
                            "baseDataMacroAfterDeleteAction"}) {
        actions()->setEnabled(id(raw), tableDesign && !m_name.isEmpty());
    }
    const bool hyperlink = db && !m_hyperlinkTarget.value(QStringLiteral("field")).toString().isEmpty();
    actions()->setEnabled(id("baseEditHyperlinkAction"),
                          hyperlink && writable && m_hyperlinkTarget.value(QStringLiteral("editable")).toBool());
    actions()->setEnabled(id("baseOpenHyperlinkAction"),
                          hyperlink && !m_hyperlinkTarget.value(QStringLiteral("text")).toString().isEmpty());
    // A data macro is designed in the macro designer but never run from it.
    if (m_view == QLatin1String("macroDesign") && m_kind == QLatin1String("dataMacro")) {
        actions()->setEnabled(id("baseMacroRunAction"), false);
    }
}

void BaseHost::setHyperlinkTarget(const QVariantMap &target)
{
    if (m_hyperlinkTarget != target) {
        m_hyperlinkTarget = target;
        emit hyperlinkTargetChanged();
        syncEnablement();
    }
}

bool BaseHost::shiftHeld() const
{
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
}

} // namespace QindaOffice::Apps::Base
