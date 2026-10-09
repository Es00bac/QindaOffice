// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Mailings tab in QindaWrite (M77, D-086), over office_docx's merge
// (docx_mailmerge.h, docx_merge_data.h, docx_merge_fields.h, docx_merge.h):
//  - Start Mail Merge: Letters, E-mail Messages, Envelopes, Labels,
//    Directory, Normal Word Document (the type is saved in w:mailMerge);
//  - Select Recipients: Type a New List (saved as CSV), Use an Existing
//    List (CSV / text, or a workbook sheet through calc_io), Choose from
//    QindaMail People (a group, read-only); Edit Recipient List (include
//    and exclude, sort, filter, find duplicates, find a recipient);
//  - Write & Insert Fields: Highlight Merge Fields, Address Block, Greeting
//    Line, Insert Merge Field (the data source's columns, twenty slots and
//    the dialog), Rules (If…Then…Else, Skip Record If, Next Record, Next
//    Record If, Merge Record #, Merge Sequence #), Match Fields, Update
//    Labels;
//  - Preview Results with First / Previous / Next / Last, the record
//    number, Find Recipient, Check for Errors;
//  - Finish & Merge: Edit Individual Documents (a new window: a section
//    per record), Print Documents, Merge to PDF, Send E-mail Messages
//    (each recipient's message handed to QindaMail's composer).
// Inserting fields is one undo step; the merge settings are saved with the
// document (not Undo's, as in Word); the merged documents are new.
//
// Files: write_mailmerge.cpp (actions, data sources, recipients),
// write_mailmerge_fields.cpp (fields, rules, address block, greeting line,
// preview), write_mailmerge_finish.cpp (Finish & Merge, e-mail hand-off).

#include "action_model.h"
#include "docx_merge.h"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class EnvelopesController;
class PageTextControl;
class PageTextItem;
class WriteDocument;

class MailMergeController final : public QObject {
    Q_OBJECT
    // letters, email, envelopes, labels, directory; "" for an ordinary document
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QStringList fields READ fields NOTIFY changed)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)
    Q_PROPERTY(int recipientCount READ recipientCount NOTIFY changed)
    Q_PROPERTY(bool previewing READ previewing NOTIFY previewChanged)
    // 1-based position of the previewed recipient in merge order
    Q_PROPERTY(int previewRecord READ previewRecord WRITE setPreviewRecord NOTIFY previewChanged)
public:
    static constexpr int kFieldSlots = 20;

    MailMergeController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                        QObject *parent);
    ~MailMergeController() override;

    void setPageItem(PageTextItem *item);
    void setEnvelopes(EnvelopesController *envelopes) { m_envelopes = envelopes; }
    // The Mailings menu.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // Hooks the host sets: open a Word package in a new window; print a
    // document; start QindaMail with arguments (tests replace it; the
    // default honours QQO_NO_HANDOFF and never sends anything itself).
    std::function<void(const QByteArray &)> openDocument;
    std::function<void(std::shared_ptr<QTextDocument>)> printDocument;
    std::function<bool(const QStringList &)> launchMail;

    [[nodiscard]] QString kind() const;
    [[nodiscard]] QStringList fields() const { return m_data.fields; }
    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] int recipientCount() const { return int(m_order.size()); }
    [[nodiscard]] bool previewing() const { return m_previewing; }
    [[nodiscard]] int previewRecord() const { return m_position + 1; }
    void setPreviewRecord(int record);
    [[nodiscard]] const Docx::MergeData &data() const { return m_data; }
    [[nodiscard]] const QList<int> &order() const { return m_order; }
    // Re-reads the data source the document names (after an open).
    void reload();

    // ---- recipients (write_mailmerge.cpp) ----
    Q_INVOKABLE bool useList(const QUrl &url);
    Q_INVOKABLE bool useWorkbookSheet(const QUrl &url, const QString &sheet);
    // [{group, label, count}] — empty when QindaMail has no People.
    Q_INVOKABLE QVariantList peopleGroups() const;
    Q_INVOKABLE bool usePeople(const QString &group);
    Q_INVOKABLE QStringList newListFields() const;
    // Type a New List: `rows` are lists of strings under `fields`, saved
    // as CSV at `url` and used as the recipients.
    Q_INVOKABLE bool saveNewList(const QUrl &url, const QStringList &fields, const QVariantList &rows);
    // Edit Recipient List: [{index, included, values}] in filter and sort order.
    Q_INVOKABLE QVariantList recipientRows() const;
    Q_INVOKABLE void setIncluded(int index, bool included);
    Q_INVOKABLE void setAllIncluded(bool included);
    Q_INVOKABLE void sortRecipients(const QString &field, bool descending);
    // {filters: [{join, field, op, value}], sorts: [{field, descending}]}
    Q_INVOKABLE QVariantMap filterSort() const;
    Q_INVOKABLE void setFilterSort(const QVariantMap &answer);
    // Find Duplicates: groups of [{index, values}].
    Q_INVOKABLE QVariantList duplicates() const;
    // Find Recipient: the next position (0-based, merge order) after
    // `from` whose `field` ("" any) contains `text`; −1 none.
    Q_INVOKABLE int findRecipient(const QString &text, const QString &field, int from) const;

    // ---- fields (write_mailmerge_fields.cpp) ----
    Q_INVOKABLE void insertField(const QString &column);
    Q_INVOKABLE QVariantMap addressBlockInfo() const;
    Q_INVOKABLE QString addressBlockPreview(const QVariantMap &options) const;
    Q_INVOKABLE void insertAddressBlock(const QVariantMap &options);
    Q_INVOKABLE QVariantMap greetingLineInfo() const;
    Q_INVOKABLE QString greetingLinePreview(const QVariantMap &options) const;
    Q_INVOKABLE void insertGreetingLine(const QVariantMap &options);
    // {rule: "if" | "skipif" | "nextif", field, op, value, then, otherwise}
    Q_INVOKABLE void insertRule(const QVariantMap &answer);
    // {fields: [{name, column}], columns}; the answer maps name → column.
    Q_INVOKABLE QVariantMap matchFieldsInfo() const;
    Q_INVOKABLE void setMatchFields(const QVariantMap &map);
    // Update Labels: the first label's content in every other label, after
    // its «Next Record»; one undo step.
    Q_INVOKABLE bool updateLabels();
    // The «AddressBlock» Start Mail Merge ▸ Envelopes puts on an envelope.
    void insertDefaultAddressBlock(QTextCursor &cursor);
    // Check for Errors: what would go wrong ("" none).
    Q_INVOKABLE QStringList checkErrors() const;

    // ---- Finish & Merge (write_mailmerge_finish.cpp) ----
    // {records: "all" | "current" | "range", from, to (1-based)}
    Q_INVOKABLE bool finishToDocument(const QVariantMap &range);
    Q_INVOKABLE bool finishToPrinter(const QVariantMap &range);
    Q_INVOKABLE bool finishToPdf(const QUrl &url, const QVariantMap &range);
    // {fields, to, subject, format}
    Q_INVOKABLE QVariantMap emailInfo() const;
    // {to (a field), subject, format: "text" | "attachment", records, from,
    //  to…}: one QindaMail composer per recipient; returns how many.
    Q_INVOKABLE int sendEmails(const QVariantMap &answer);
    // The merged document for `range` (laid out like the main document).
    [[nodiscard]] std::shared_ptr<QTextDocument> mergedDocument(const QVariantMap &range, Docx::MergeResult *result);

signals:
    void changed();
    void previewChanged();
    void message(const QString &text);
    void newListRequested();
    void existingListRequested();
    void peopleRequested();
    void recipientsRequested();
    void sheetRequested(const QUrl &url, const QStringList &sheets);
    void addressBlockRequested();
    void greetingLineRequested();
    void insertFieldRequested();
    void ruleRequested(const QString &rule);
    void matchFieldsRequested();
    void findRequested();
    void errorsReported(const QStringList &errors);
    // "document", "printer", "pdf", "email"
    void finishRequested(const QString &target);

private:
    // write_mailmerge.cpp
    void createActions(Quick::ActionModel *model);
    void setKind(const QString &kind);
    void adopt(Docx::MergeData data, const QString &table = QString());
    void refreshOrder();
    void sync();
    [[nodiscard]] Docx::MailMergeSettings settings() const;
    void store(const Docx::MailMergeSettings &settings);
    // write_mailmerge_fields.cpp
    void insertCode(const QString &instruction);
    void setPreviewing(bool on);
    void refreshPreview();
    // write_mailmerge_finish.cpp
    [[nodiscard]] std::pair<int, int> rangeOf(const QVariantMap &range) const;

    PageTextControl *m_control;
    WriteDocument *m_document;
    QPointer<PageTextItem> m_page;
    EnvelopesController *m_envelopes = nullptr;
    Docx::MergeData m_data;
    QList<int> m_order;
    bool m_previewing = false;
    int m_position = 0;
    std::unique_ptr<QTextDocument> m_preview;
    QList<Quick::ActionEntry *> m_kinds;
    QList<Quick::ActionEntry *> m_slots;
    QList<Quick::ActionEntry *> m_needData; // enabled with recipients
    Quick::ActionEntry *m_highlight = nullptr;
    Quick::ActionEntry *m_previewAction = nullptr;
    Quick::ActionEntry *m_updateLabels = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
