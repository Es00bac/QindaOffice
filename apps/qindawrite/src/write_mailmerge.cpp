// SPDX-License-Identifier: GPL-3.0-or-later
// MailMergeController (write_mailmerge.h): the Mailings commands, the merge
// type, the data sources and the recipient list.

#include "write_mailmerge.h"

#include "menu_tree.h"
#include "page_text_control.h"
#include "page_text_item.h"
#include "write_document.h"
#include "write_envelopes.h"
#include "write_merge_sources.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *icon;
};

constexpr Command kKinds[] = {{"writeMergeLettersAction", "&Letters", "file-text"},
                              {"writeMergeEmailAction", "&E-mail Messages", "mail-plus"},
                              {"writeMergeEnvelopesAction", "En&velopes…", "mail"},
                              {"writeMergeLabelsAction", "La&bels…", "tag"},
                              {"writeMergeDirectoryAction", "&Directory", "list"},
                              {"writeMergeNormalAction", "&Normal Word Document", "file"}};
constexpr const char *kKindNames[] = {"letters", "email", "envelopes", "labels", "directory", ""};

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

QVariantMap filterMap(const Docx::MergeFilter &f)
{
    return {{QStringLiteral("join"), f.join}, {QStringLiteral("field"), f.field}, {QStringLiteral("op"), f.op},
            {QStringLiteral("value"), f.value}};
}

} // namespace

MailMergeController::MailMergeController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                         QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    // AGENT-GUARD: QQO_NO_HANDOFF (set by the tests) starts nothing — a
    // detached QindaMail would outlive the test and hold its output pipe.
    // QindaMail only opens composer drafts: nothing is ever sent from here.
    launchMail = [](const QStringList &arguments) {
        if (qEnvironmentVariableIsSet("QQO_NO_HANDOFF")) {
            return false;
        }
        const QString mail = QStandardPaths::findExecutable(QStringLiteral("qindamail"));
        return !mail.isEmpty() && QProcess::startDetached(mail, arguments);
    };
    createActions(model);
    reload();
}

MailMergeController::~MailMergeController()
{
    if (m_page != nullptr) {
        m_page->setPreviewDocument(nullptr);
    }
}

void MailMergeController::setPageItem(PageTextItem *item)
{
    m_page = item;
    refreshPreview();
}

void MailMergeController::createActions(Quick::ActionModel *model)
{
    const auto make = [this, model](const char *id, const QString &text, const char *icon, bool needsData,
                                    const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        connect(entry, &ActionEntry::triggered, this, [this, run] {
            if (!m_syncing) {
                run();
            }
        });
        if (needsData) {
            m_needData.append(entry);
        }
        return entry;
    };
    for (int i = 0; i < int(std::size(kKinds)); ++i) {
        ActionEntry *entry = make(kKinds[i].id, tr(kKinds[i].text), kKinds[i].icon, false,
                                  [this, i] { setKind(QString::fromLatin1(kKindNames[i])); });
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeMergeKind"));
        m_kinds.append(entry);
    }
    make("writeMergeNewListAction", tr("&Type a New List…"), "user-plus", false, [this] { emit newListRequested(); });
    make("writeMergeExistingListAction", tr("Use an &Existing List…"), "file-spreadsheet", false,
         [this] { emit existingListRequested(); });
    make("writeMergePeopleAction", tr("Choose from QindaMail &People…"), "contact", false,
         [this] { emit peopleRequested(); });
    make("writeMergeEditRecipientsAction", tr("Edit Recipient &List…"), "list-checks", true,
         [this] { emit recipientsRequested(); });
    m_highlight = make("writeMergeHighlightAction", tr("&Highlight Merge Fields"), "highlighter", false, [this] {
        if (m_page != nullptr) {
            m_page->setMergeHighlight(m_highlight->isChecked());
        }
    });
    m_highlight->setCheckable(true);
    make("writeMergeAddressBlockAction", tr("&Address Block…"), "book-user", true, [this] { emit addressBlockRequested(); });
    make("writeMergeGreetingLineAction", tr("&Greeting Line…"), "handshake", true, [this] { emit greetingLineRequested(); });
    make("writeMergeInsertFieldAction", tr("Insert Merge &Field…"), "braces", true, [this] { emit insertFieldRequested(); });
    for (int i = 0; i < kFieldSlots; ++i) {
        ActionEntry *slot = make(QStringLiteral("writeMergeField%1").arg(i).toLatin1().constData(), QString(), "braces",
                                 false, [this, i] { insertField(m_data.fields.value(i)); });
        m_slots.append(slot);
    }
    make("writeMergeRuleIfAction", tr("&If…Then…Else…"), "git-branch", true, [this] { emit ruleRequested(QStringLiteral("if")); });
    make("writeMergeRuleSkipIfAction", tr("&Skip Record If…"), "skip-forward", true,
         [this] { emit ruleRequested(QStringLiteral("skipif")); });
    make("writeMergeRuleNextAction", tr("&Next Record"), "step-forward", true, [this] { insertCode(QStringLiteral("NEXT")); });
    make("writeMergeRuleNextIfAction", tr("Next Record I&f…"), "chevrons-right", true,
         [this] { emit ruleRequested(QStringLiteral("nextif")); });
    make("writeMergeRuleRecordAction", tr("Merge &Record #"), "hash", true, [this] { insertCode(QStringLiteral("MERGEREC")); });
    make("writeMergeRuleSequenceAction", tr("Merge Se&quence #"), "list-ordered", true,
         [this] { insertCode(QStringLiteral("MERGESEQ")); });
    make("writeMergeMatchFieldsAction", tr("&Match Fields…"), "link-2", true, [this] { emit matchFieldsRequested(); });
    m_updateLabels = make("writeMergeUpdateLabelsAction", tr("&Update Labels"), "refresh-cw", false, [this] { updateLabels(); });
    m_previewAction = make("writeMergePreviewAction", tr("&Preview Results"), "eye", true,
                           [this] { setPreviewing(m_previewAction->isChecked()); });
    m_previewAction->setCheckable(true);
    make("writeMergeFirstAction", tr("&First Record"), "chevrons-left", true, [this] { setPreviewRecord(1); });
    make("writeMergePreviousAction", tr("Pre&vious Record"), "chevron-left", true,
         [this] { setPreviewRecord(previewRecord() - 1); });
    make("writeMergeNextAction", tr("Ne&xt Record"), "chevron-right", true, [this] { setPreviewRecord(previewRecord() + 1); });
    make("writeMergeLastAction", tr("Las&t Record"), "chevrons-right", true,
         [this] { setPreviewRecord(int(m_order.size())); });
    make("writeMergeFindAction", tr("Find &Recipient…"), "search", true, [this] { emit findRequested(); });
    make("writeMergeCheckAction", tr("Check for &Errors…"), "circle-check", true,
         [this] { emit errorsReported(checkErrors()); });
    make("writeMergeEditDocumentsAction", tr("Edit &Individual Documents…"), "files", true,
         [this] { emit finishRequested(QStringLiteral("document")); });
    make("writeMergePrintAction", tr("&Print Documents…"), "printer", true,
         [this] { emit finishRequested(QStringLiteral("printer")); });
    make("writeMergePdfAction", tr("Merge to P&DF…"), "file-down", true, [this] { emit finishRequested(QStringLiteral("pdf")); });
    make("writeMergeEmailSendAction", tr("Send E-mail &Messages…"), "send", true,
         [this] { emit finishRequested(QStringLiteral("email")); });
    sync();
}

QList<MenuNode> MailMergeController::menuItems() const
{
    QList<MenuNode> kinds;
    for (const ActionEntry *entry : m_kinds) {
        kinds.append(MenuNode::action(entry->id()));
    }
    QList<MenuNode> fieldSlots;
    for (const ActionEntry *entry : m_slots) {
        fieldSlots.append(MenuNode::action(entry->id()));
    }
    fieldSlots.append(MenuNode::separator());
    fieldSlots.append(a("writeMergeInsertFieldAction"));
    QList<MenuNode> items;
    if (m_envelopes != nullptr) {
        items += m_envelopes->menuItems();
        items.append(MenuNode::separator());
    }
    items += {MenuNode::submenu(QStringLiteral("writeMergeStartMenu"), tr("&Start Mail Merge"), kinds),
              MenuNode::submenu(QStringLiteral("writeMergeRecipientsMenu"), tr("Select &Recipients"),
                                {a("writeMergeNewListAction"), a("writeMergeExistingListAction"), a("writeMergePeopleAction")}),
              a("writeMergeEditRecipientsAction"),
              MenuNode::separator(),
              a("writeMergeHighlightAction"),
              a("writeMergeAddressBlockAction"),
              a("writeMergeGreetingLineAction"),
              MenuNode::submenu(QStringLiteral("writeMergeFieldMenu"), tr("Insert Merge &Field"), fieldSlots),
              MenuNode::submenu(QStringLiteral("writeMergeRulesMenu"), tr("R&ules"),
                                {a("writeMergeRuleIfAction"), a("writeMergeRuleSkipIfAction"), a("writeMergeRuleNextAction"),
                                 a("writeMergeRuleNextIfAction"), a("writeMergeRuleRecordAction"),
                                 a("writeMergeRuleSequenceAction")}),
              a("writeMergeMatchFieldsAction"),
              a("writeMergeUpdateLabelsAction"),
              MenuNode::separator(),
              a("writeMergePreviewAction"),
              a("writeMergeFirstAction"),
              a("writeMergePreviousAction"),
              a("writeMergeNextAction"),
              a("writeMergeLastAction"),
              a("writeMergeFindAction"),
              a("writeMergeCheckAction"),
              MenuNode::separator(),
              MenuNode::submenu(QStringLiteral("writeMergeFinishMenu"), tr("Finish && &Merge"),
                                {a("writeMergeEditDocumentsAction"), a("writeMergePrintAction"), a("writeMergePdfAction"),
                                 a("writeMergeEmailSendAction")})};
    return items;
}

QString MailMergeController::kind() const
{
    return Docx::mergeKind(m_document->mailMerge().documentType);
}

QString MailMergeController::sourceName() const
{
    const QString source = m_data.source;
    if (source.startsWith(u"qindamail:people")) {
        const QString group = source.section(QLatin1Char('/'), 1);
        return group.isEmpty() ? tr("QindaMail People") : tr("QindaMail People: %1").arg(group);
    }
    return QFileInfo(source).fileName();
}

Docx::MailMergeSettings MailMergeController::settings() const
{
    return m_document->mailMerge();
}

void MailMergeController::store(const Docx::MailMergeSettings &settings)
{
    m_document->setMailMerge(settings);
    refreshOrder();
}

void MailMergeController::setKind(const QString &kind)
{
    Docx::MailMergeSettings s = settings();
    s.documentType = Docx::wordDocumentType(kind);
    store(s);
    if (kind.isEmpty()) {
        setPreviewing(false);
    } else if (kind == u"envelopes" && m_envelopes != nullptr) {
        emit m_envelopes->envelopeDialogRequested(true);
    } else if (kind == u"labels" && m_envelopes != nullptr) {
        emit m_envelopes->labelsDialogRequested(true);
    }
    sync();
}

void MailMergeController::reload()
{
    const Docx::MailMergeSettings s = settings();
    Docx::MergeData data;
    QString error;
    if (s.dataSource.startsWith(u"qindamail:people")) {
        data = MergeSources::people(s.dataSource.section(QLatin1Char('/'), 1), &error);
    } else if (!s.dataSource.isEmpty() && QFileInfo::exists(s.dataSource)) {
        data = MergeSources::read(s.dataSource, s.table, &error);
    } else if (!s.dataSource.isEmpty()) {
        emit message(tr("The merge's data source %1 was not found; use Select Recipients to choose it again.")
                         .arg(s.dataSource));
    }
    m_data = data;
    m_position = qMax(0, s.activeRecord - 1);
    refreshOrder();
}

void MailMergeController::adopt(Docx::MergeData data, const QString &table)
{
    Docx::MailMergeSettings s = settings();
    s.dataSource = data.source;
    s.table = table;
    s.dataType = QStringLiteral("textFile");
    s.filters.clear();
    s.sorts.clear();
    s.excluded.clear();
    s.fieldMap.clear();
    s.query.clear();
    if (s.documentType.isEmpty()) {
        s.documentType = QStringLiteral("formLetters"); // Word: recipients make a letters merge
    }
    m_data = std::move(data);
    m_position = 0;
    store(s);
    emit message(tr("%n recipient(s) from %1", "", int(m_data.records.size())).arg(sourceName()));
}

void MailMergeController::refreshOrder()
{
    m_order = Docx::recipientOrder(m_data, settings());
    m_position = qBound(0, m_position, qMax(0, int(m_order.size()) - 1));
    sync();
    refreshPreview();
    emit changed();
}

void MailMergeController::sync()
{
    m_syncing = true;
    const QString current = kind();
    for (int i = 0; i < m_kinds.size(); ++i) {
        m_kinds.at(i)->setChecked(current == QLatin1String(kKindNames[i]));
    }
    const bool data = !m_data.isEmpty();
    for (ActionEntry *entry : std::as_const(m_needData)) {
        entry->setEnabled(data);
    }
    for (int i = 0; i < m_slots.size(); ++i) {
        const QString name = m_data.fields.value(i);
        m_slots.at(i)->setText(name.isEmpty() ? tr("(no field)") : name);
        m_slots.at(i)->setVisible(!name.isEmpty());
        m_slots.at(i)->setEnabled(!name.isEmpty());
    }
    m_updateLabels->setEnabled(current == u"labels");
    m_previewAction->setChecked(m_previewing);
    m_syncing = false;
}

bool MailMergeController::useList(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (MergeSources::isWorkbook(path)) {
        const QStringList sheets = MergeSources::sheets(path);
        if (sheets.size() > 1) {
            emit sheetRequested(url, sheets); // Word's Select Table
            return true;
        }
        return useWorkbookSheet(url, sheets.value(0));
    }
    QString error;
    Docx::MergeData data = MergeSources::read(path, QString(), &error);
    if (data.isEmpty()) {
        emit message(tr("Could not read the recipients: %1").arg(error));
        return false;
    }
    adopt(std::move(data));
    return true;
}

bool MailMergeController::useWorkbookSheet(const QUrl &url, const QString &sheet)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    Docx::MergeData data = MergeSources::read(path, sheet, &error);
    if (data.isEmpty()) {
        emit message(tr("Could not read the recipients: %1").arg(error));
        return false;
    }
    adopt(std::move(data), sheet.isEmpty() ? MergeSources::sheets(path).value(0) : sheet);
    return true;
}

QVariantList MailMergeController::peopleGroups() const
{
    QVariantList out;
    QString error;
    for (const auto &[group, count] : MergeSources::peopleGroups(&error)) {
        out.append(QVariantMap{{QStringLiteral("group"), group},
                               {QStringLiteral("label"), group.isEmpty() ? tr("All contacts") : group},
                               {QStringLiteral("count"), count}});
    }
    return out;
}

bool MailMergeController::usePeople(const QString &group)
{
    QString error;
    Docx::MergeData data = MergeSources::people(group, &error);
    if (data.records.isEmpty()) {
        emit message(error.isEmpty() ? tr("No contacts in that group") : error);
        return false;
    }
    adopt(std::move(data));
    return true;
}

QStringList MailMergeController::newListFields() const
{
    return MergeSources::newListFields();
}

bool MailMergeController::saveNewList(const QUrl &url, const QStringList &fields, const QVariantList &rows)
{
    Docx::MergeData data;
    data.fields = fields;
    for (const QVariant &row : rows) {
        QStringList record = row.toStringList();
        while (record.size() < fields.size()) {
            record.append(QString());
        }
        data.records.append(record.mid(0, fields.size()));
    }
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".csv");
    }
    QString error;
    if (!Docx::writeCsvFile(path, data, &error)) {
        emit message(tr("Could not save the list: %1").arg(error));
        return false;
    }
    data.source = path;
    adopt(std::move(data));
    return true;
}

QVariantList MailMergeController::recipientRows() const
{
    QVariantList rows;
    const Docx::MailMergeSettings s = settings();
    for (const int index : Docx::filteredOrder(m_data, s)) {
        rows.append(QVariantMap{{QStringLiteral("index"), index},
                                {QStringLiteral("included"), !s.excluded.contains(index)},
                                {QStringLiteral("values"), m_data.records.at(index)}});
    }
    return rows;
}

void MailMergeController::setIncluded(int index, bool included)
{
    Docx::MailMergeSettings s = settings();
    s.excluded.removeAll(index);
    if (!included && index >= 0 && index < m_data.records.size()) {
        s.excluded.append(index);
        std::sort(s.excluded.begin(), s.excluded.end());
    }
    store(s);
}

void MailMergeController::setAllIncluded(bool included)
{
    Docx::MailMergeSettings s = settings();
    s.excluded.clear();
    if (!included) {
        for (int i = 0; i < m_data.records.size(); ++i) {
            s.excluded.append(i);
        }
    }
    store(s);
}

void MailMergeController::sortRecipients(const QString &field, bool descending)
{
    Docx::MailMergeSettings s = settings();
    s.sorts = {{field, descending}};
    store(s);
}

QVariantMap MailMergeController::filterSort() const
{
    QVariantList filters;
    QVariantList sorts;
    const Docx::MailMergeSettings s = settings();
    for (const Docx::MergeFilter &f : s.filters) {
        filters.append(filterMap(f));
    }
    for (const Docx::MergeSort &sort : s.sorts) {
        sorts.append(QVariantMap{{QStringLiteral("field"), sort.field}, {QStringLiteral("descending"), sort.descending}});
    }
    return {{QStringLiteral("filters"), filters}, {QStringLiteral("sorts"), sorts}, {QStringLiteral("fields"), m_data.fields}};
}

void MailMergeController::setFilterSort(const QVariantMap &answer)
{
    Docx::MailMergeSettings s = settings();
    s.filters.clear();
    s.sorts.clear();
    for (const QVariant &v : answer.value(QStringLiteral("filters")).toList()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("field")).toString().isEmpty()) {
            continue;
        }
        s.filters.append({m.value(QStringLiteral("join"), QStringLiteral("and")).toString(),
                          m.value(QStringLiteral("field")).toString(),
                          m.value(QStringLiteral("op"), QStringLiteral("equal")).toString(),
                          m.value(QStringLiteral("value")).toString()});
    }
    for (const QVariant &v : answer.value(QStringLiteral("sorts")).toList()) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("field")).toString().isEmpty()) {
            s.sorts.append({m.value(QStringLiteral("field")).toString(), m.value(QStringLiteral("descending")).toBool()});
        }
    }
    store(s);
}

QVariantList MailMergeController::duplicates() const
{
    QVariantList groups;
    const QList<int> excluded = settings().excluded;
    for (const QList<int> &group : Docx::duplicateGroups(m_data)) {
        QVariantList rows;
        for (const int index : group) {
            rows.append(QVariantMap{{QStringLiteral("index"), index},
                                    {QStringLiteral("values"), m_data.records.at(index)},
                                    {QStringLiteral("included"), !excluded.contains(index)}});
        }
        groups.append(QVariant(rows));
    }
    return groups;
}

int MailMergeController::findRecipient(const QString &text, const QString &field, int from) const
{
    if (text.isEmpty()) {
        return -1;
    }
    for (int step = 1; step <= m_order.size(); ++step) {
        const int position = (from + step) % int(m_order.size());
        const QStringList &record = m_data.records.at(m_order.at(position));
        for (int c = 0; c < record.size(); ++c) {
            if ((field.isEmpty() || m_data.fields.value(c) == field) && record.at(c).contains(text, Qt::CaseInsensitive)) {
                return position;
            }
        }
    }
    return -1;
}

} // namespace QindaOffice::Apps::Write
