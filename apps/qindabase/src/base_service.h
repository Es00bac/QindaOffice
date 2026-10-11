// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BaseService: the whole QML API of a QindaBase window over base_engine
// (M49, D-049 §10). The host owns it; QML reaches it as `host.base`.
//
// AGENT-CONTRACT (with the QML in qml/ — both sides written together):
//  * Results are maps with fixed keys: {ok, error, …}. Nothing throws.
//  * Every data or design change the user makes is ONE step on the
//    document's undo stack (BaseDocument::pushChange); schema changes are
//    previewed first (planTableDesign → applyTableDesign) and back up the
//    database before anything destructive.
//  * `objects` is the navigation pane: [{kind, name, description,
//    iconName}] for tables, queries, forms and reports, refreshed on
//    objectsChanged(). A plain SQLite file has tables only until the user
//    agrees to add QindaBase's system tables (needsSystemTables results).
//  * Money crosses as display text; typed input is text parsed by the
//    engine. QML never does arithmetic on records.
//  * Forms and reports open through scratch presentation databases the
//    service writes (a form mirror, a report snapshot) plus a design file
//    QML opens with Tk.DocumentManager, so the toolkit's designers start
//    with a clean history (base_snapshot.h, D-049 §8).
//
// Files: base_service.cpp (file, navigation, objects, settings),
// base_service_tables.cpp (table design, relationships, SQL console),
// base_service_queries.cpp (queries), base_service_query_tools.cpp (M80:
// the Access SQL view, make-table runs, rule tests), base_service_forms.cpp
// (forms), base_service_form_controls.cpp (M80: lookups as combo and list
// boxes, subforms, buttons), base_service_macros.cpp (M80: macros and where
// conditions), base_service_reports.cpp (reports),
// base_service_report_output.cpp (M80: reports evaluated by the engine,
// mailing labels), base_service_import.cpp (CSV, Excel and Access import),
// base_service_io.cpp (export, maintenance), base_service_files.cpp (file
// fields); M86 (D-094): base_service_access_tools.cpp (query wizards, data
// macros, the application log, hyperlinks, report levels, AutoExec) and
// base_service_links.cpp (Access's XML import, linked tables).

#include "datasheet_model.h"

#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Base {
struct CsvTable;
struct QueryDesign;
struct ReportSource;
}

namespace QindaOffice::Apps::Base {

class BaseDocument;
class LabelSheet;

class BaseService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by BaseHost.")
    Q_MOC_INCLUDE("label_sheet.h")
    Q_PROPERTY(bool hasDatabase READ hasDatabase NOTIFY fileChanged)
    Q_PROPERTY(QString databaseName READ databaseName NOTIFY fileChanged)
    Q_PROPERTY(QString workingPath READ workingPath NOTIFY fileChanged)
    Q_PROPERTY(bool hasSystemTables READ hasSystemTables NOTIFY objectsChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY fileChanged)
    Q_PROPERTY(QStringList openProblems READ openProblems NOTIFY fileChanged)
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)
    Q_PROPERTY(QStringList tableNames READ tableNames NOTIFY objectsChanged)
    Q_PROPERTY(QStringList queryNames READ queryNames NOTIFY objectsChanged)
    // M80: the objects macro arguments and form buttons pick from.
    Q_PROPERTY(QStringList formNames READ formNames NOTIFY objectsChanged)
    Q_PROPERTY(QStringList reportNames READ reportNames NOTIFY objectsChanged)
    Q_PROPERTY(QStringList macroNames READ macroNames NOTIFY objectsChanged)
    Q_PROPERTY(QindaOffice::Apps::Base::DatasheetModel *datasheet READ datasheet CONSTANT)
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    Q_PROPERTY(QVariantList fieldTypes READ fieldTypes CONSTANT)
    // M80 catalogs: Access's input mask presets, macro actions, label
    // products and the types Query Parameters declares.
    Q_PROPERTY(QVariantList inputMaskPresets READ inputMaskPresets CONSTANT)
    Q_PROPERTY(QVariantList macroActions READ macroActions CONSTANT)
    Q_PROPERTY(QVariantList labelProducts READ labelProducts CONSTANT)
    Q_PROPERTY(QVariantList parameterTypes READ parameterTypes CONSTANT)
    // M86: the table events and the actions a data macro takes.
    Q_PROPERTY(QVariantList dataMacroEvents READ dataMacroEvents CONSTANT)
    Q_PROPERTY(QVariantList dataMacroActions READ dataMacroActions CONSTANT)
    // M80: the label sheet a label report shows and prints (label_sheet.h).
    Q_PROPERTY(QindaOffice::Apps::Base::LabelSheet *labels READ labels CONSTANT)
public:
    explicit BaseService(BaseDocument *document, QObject *parent = nullptr);
    ~BaseService() override;

    [[nodiscard]] bool hasDatabase() const;
    [[nodiscard]] QString databaseName() const;
    [[nodiscard]] QString workingPath() const;
    [[nodiscard]] bool hasSystemTables() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QStringList openProblems() const;
    [[nodiscard]] QVariantList objects() const;
    [[nodiscard]] QStringList tableNames() const;
    [[nodiscard]] QStringList queryNames() const;
    [[nodiscard]] QStringList formNames() const;
    [[nodiscard]] QStringList reportNames() const;
    [[nodiscard]] QStringList macroNames() const;
    [[nodiscard]] DatasheetModel *datasheet() const { return m_datasheet; }
    [[nodiscard]] QVariantList templates() const;
    [[nodiscard]] QVariantList fieldTypes() const;
    [[nodiscard]] QVariantList inputMaskPresets() const;
    [[nodiscard]] QVariantList macroActions() const;
    [[nodiscard]] QVariantList labelProducts() const;
    [[nodiscard]] QVariantList parameterTypes() const;
    [[nodiscard]] QVariantList dataMacroEvents() const;
    [[nodiscard]] QVariantList dataMacroActions() const;
    [[nodiscard]] LabelSheet *labels() const { return m_labels; }
    [[nodiscard]] QindaOffice::Base::BaseFile *file() const;

    // ---- database ----
    Q_INVOKABLE QVariantMap createUntitled(const QString &templateId);
    Q_INVOKABLE QVariantMap addSystemTables();
    Q_INVOKABLE QVariantMap properties() const;
    Q_INVOKABLE QVariantMap setProperties(const QVariantMap &values);
    Q_INVOKABLE QVariantMap objectInfo(const QString &kind, const QString &name) const;
    Q_INVOKABLE QVariantMap renameObject(const QString &kind, const QString &from, const QString &to);
    // What deleting an object means (rows, dependent objects, blockers).
    Q_INVOKABLE QVariantMap deleteObjectImpact(const QString &kind, const QString &name) const;
    Q_INVOKABLE QVariantMap deleteObject(const QString &kind, const QString &name);

    // ---- tables and relationships (base_service_tables.cpp) ----
    Q_INVOKABLE QVariantMap tableDesign(const QString &name) const;
    Q_INVOKABLE QVariantMap newTableDesign(const QString &name) const;
    Q_INVOKABLE QVariantMap planTableDesign(const QVariantMap &design);
    Q_INVOKABLE QVariantMap applyTableDesign(const QVariantMap &design);
    Q_INVOKABLE QVariantMap quickTable(const QString &name);
    Q_INVOKABLE QVariantList tableFields(const QString &table) const;
    Q_INVOKABLE QVariantList relationships() const;
    Q_INVOKABLE QVariantMap planRelationship(const QVariantMap &relationship, bool add);
    Q_INVOKABLE QVariantMap applyRelationship(const QVariantMap &relationship, bool add);
    // Database Tools ▸ SQL: schema statements go through a previewed plan.
    Q_INVOKABLE QVariantMap planSql(const QStringList &statements);
    Q_INVOKABLE QVariantMap applySql(const QStringList &statements);
    // M80: Table Design ▸ Test Validation Rules — the saved table's records
    // against its field and record rules: {ok, lines, broken}.
    Q_INVOKABLE QVariantMap testValidationRules(const QString &table) const;

    // ---- queries (base_service_queries.cpp) ----
    Q_INVOKABLE QVariantMap queryDesign(const QString &name) const;
    Q_INVOKABLE QVariantMap newQueryDesign(const QStringList &tables) const;
    Q_INVOKABLE QVariantList suggestJoins(const QStringList &tables) const;
    Q_INVOKABLE QVariantMap compileQuery(const QVariantMap &design) const;
    Q_INVOKABLE QVariantMap saveQuery(const QString &name, const QVariantMap &design);
    // Runs a select into the datasheet, or reports {action:true, affected}
    // for an action query without running it.
    Q_INVOKABLE QVariantMap runQuery(const QString &name, const QVariantMap &design, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap runAction(const QVariantMap &design, const QVariantMap &parameters, qint64 confirmedCount);
    // Database Tools ▸ SQL: a select's first rows as display text
    // {ok, columns, rows, truncated} — or {needsParameters, parameters}.
    Q_INVOKABLE QVariantMap previewSql(const QString &sql, const QVariantMap &parameters, int limit = 500) const;
    // M80 (base_service_query_tools.cpp): the SQL view in Access SQL —
    // {ok, sql} for a design, {ok, design} (or {ok:false, error}) for text.
    Q_INVOKABLE QVariantMap accessSql(const QVariantMap &design) const;
    Q_INVOKABLE QVariantMap parseAccessSql(const QString &sql) const;
    // A make-table query's plan (runQuery returns it to be confirmed), run
    // as one undoable schema step: {ok, rows, name, backupPath}.
    Q_INVOKABLE QVariantMap runMakeTable(const QVariantMap &design, const QVariantMap &parameters);

    // ---- macros (M80, base_service_macros.cpp, base_macro.h) ----
    Q_INVOKABLE QVariantMap macroDesign(const QString &name) const;
    Q_INVOKABLE QVariantMap newMacroDesign() const;
    // {ok, problems} — what keeps the macro from running.
    Q_INVOKABLE QVariantMap checkMacro(const QVariantMap &macro, const QString &name = {}) const;
    Q_INVOKABLE QVariantMap saveMacro(const QString &name, const QVariantMap &macro);
    // The rowids of `table` meeting an Access where condition (OpenForm,
    // ApplyFilter): {ok, rowIds} or {ok:false, error}.
    Q_INVOKABLE QVariantMap whereRowIds(const QString &table, const QString &condition) const;

    // ---- forms and reports (base_service_forms.cpp) ----
    Q_INVOKABLE QVariantMap createForm(const QString &name, const QString &table, const QString &title,
                                       const QStringList &fields);
    Q_INVOKABLE QVariantMap openForm(const QString &name);
    Q_INVOKABLE QVariantMap saveFormDesign(const QString &name, const QVariantMap &design);
    Q_INVOKABLE QVariantList formRowIds(const QString &table) const;
    Q_INVOKABLE QVariantMap formRecord(const QString &table, qint64 rowid) const;
    Q_INVOKABLE QVariantMap saveFormRecord(const QString &table, qint64 rowid, const QVariantMap &changes);
    Q_INVOKABLE QVariantMap deleteFormRecord(const QString &table, qint64 rowid);
    Q_INVOKABLE QVariantMap createReport(const QString &name, const QVariantMap &spec);
    Q_INVOKABLE QVariantList reportParameters(const QString &name) const;
    // Writes the report's snapshot and (unless `dataOnly`) its design file.
    Q_INVOKABLE QVariantMap openReport(const QString &name, const QVariantMap &parameters, bool dataOnly = false);
    Q_INVOKABLE QVariantMap saveReportDesign(const QString &name, const QVariantMap &design);
    Q_INVOKABLE QVariantList sourceFields(const QString &kind, const QString &name) const;

    // ---- form controls (M80 base_service_form_controls.cpp; M89, D-097,
    // base_service_form_layout.cpp: QindaTK's positioned controls) ----
    // A lookup field's choices [{value, text}] (the related records).
    Q_INVOKABLE QVariantList lookupChoices(const QString &table, const QString &field) const;
    // The datasheet a form's subform `index` (0..3) shows (owned here).
    Q_INVOKABLE QindaOffice::Apps::Base::DatasheetModel *subformSheet(int index);
    // A combo or list box's items on a lookup field: {ok, items (labels,
    // base_form_layout.h lookupLabel), keys, lookup (false: not a lookup)}.
    Q_INVOKABLE QVariantMap lookupItems(const QString &table, const QString &field) const;
    // Every lookup field of `table` at once: {field: {items, keys, required}}.
    Q_INVOKABLE QVariantMap formLookups(const QString &table) const;
    // A calculated control's value over a record as the form shows it:
    // {ok, text} or {ok:false, error}.
    Q_INVOKABLE QVariantMap evaluateFormExpression(const QString &table, const QString &expression,
                                                   const QVariantMap &record) const;
    // Add Subform's choices: one per relationship whose parent is the
    // form's table, [{caption, table, formName, linkChild, linkParent}].
    Q_INVOKABLE QVariantList subformChoices(const QString &form) const;
    // The table a subform control's form name shows: {ok, table} — a form's
    // source table, or a table of that name.
    Q_INVOKABLE QVariantMap subformTable(const QString &formName) const;

    // ---- M80 report output (base_service_report_output.cpp) ----
    // Evaluates the report map (QindaTK's design report) over the records
    // openReport read and lays it out into `document` (a QindaTK
    // RichTextDocument): {ok, rowCount, groupCount, bands} or {ok:false, error}.
    Q_INVOKABLE QVariantMap buildReport(QObject *document, const QVariantMap &report);
    // Mailing labels (base_labels.h): a label report from the Label Wizard,
    // its first labels for the wizard's preview, and the sheet it prints.
    Q_INVOKABLE QVariantMap createLabels(const QString &name, const QVariantMap &spec);
    Q_INVOKABLE QVariantMap previewLabels(const QVariantMap &spec) const;
    Q_INVOKABLE QVariantMap openLabels(const QString &name, const QVariantMap &parameters);
    // A report object's own shape: {ok, type: "report" | "labels", source}.
    Q_INVOKABLE QVariantMap reportInfo(const QString &name) const;

    // ---- M86 Access tools (base_service_access_tools.cpp) ----
    // A query wizard's design ({ok, design} or {ok:false, error}): kind
    // simple | crosstab | duplicates | unmatched, `spec` the wizard's answers
    // (base_query_wizards.h's specs as maps).
    Q_INVOKABLE QVariantMap queryWizard(const QString &kind, const QVariantMap &spec) const;
    // A table event's data macro: {ok, table, event, label, description,
    // actions}; checked; saved as one undoable schema step (base_data_macro.h).
    Q_INVOKABLE QVariantMap dataMacroDesign(const QString &table, const QString &event) const;
    Q_INVOKABLE QVariantMap checkDataMacro(const QString &table, const QString &event, const QVariantMap &macro) const;
    Q_INVOKABLE QVariantMap saveDataMacro(const QString &table, const QString &event, const QVariantMap &macro);
    // The application log (LogEvent's lines) in the datasheet.
    Q_INVOKABLE QVariantMap openApplicationLog();
    // A hyperlink's parts {display, address, subAddress, screenTip, target,
    // shown} and the text that stores them (base_hyperlink.h).
    Q_INVOKABLE QVariantMap hyperlinkParts(const QString &text) const;
    Q_INVOKABLE QString hyperlinkText(const QVariantMap &parts) const;
    // A report map's Group, Sort and Total levels, and the map with new
    // levels (base_report.h: "accessGroups" beside the toolkit's "groups").
    Q_INVOKABLE QVariantList reportLevels(const QVariantMap &report) const;
    Q_INVOKABLE QVariantMap reportWithLevels(const QVariantMap &report, const QVariantList &levels) const;
    // The macro Access runs when a database opens ("AutoExec", any case), or "".
    Q_INVOKABLE QString autoExecMacro() const;

    // ---- M86 XML import and linked tables (base_service_links.cpp) ----
    // The tables an Access XML file holds: {ok, tables:[{name, fields,
    // records}], schema}; the import is one undo step and returns the
    // Access import's report.
    Q_INVOKABLE QVariantMap xmlTables(const QUrl &url) const;
    Q_INVOKABLE QVariantMap importXml(const QUrl &url, const QStringList &tables);
    // What a link can name in a file: a database's tables or a workbook's
    // sheets — {ok, tables} / {ok, sheets}.
    Q_INVOKABLE QVariantMap linkChoices(const QUrl &url, const QString &kind) const;
    // A new linked table (spec {kind: qbase|csv|xlsx, url, table, sheet,
    // delimiter, headerRow}) as one undo step: {ok, rows}.
    Q_INVOKABLE QVariantMap linkTable(const QString &name, const QVariantMap &spec);
    // [{table, kind, source, path, refreshed, rows, problem}]
    Q_INVOKABLE QVariantList linkedTables() const;
    // The link of `table` ({linked:false} for an ordinary table).
    Q_INVOKABLE QVariantMap linkInfo(const QString &table) const;
    Q_INVOKABLE QVariantMap refreshLink(const QString &table);
    Q_INVOKABLE QVariantMap refreshAllLinks();
    Q_INVOKABLE QVariantMap relinkTable(const QString &table, const QUrl &url);
    Q_INVOKABLE QVariantMap unlinkTable(const QString &table);
    // What the refresh at open found ([{table, ok, error, rows}]).
    Q_INVOKABLE QVariantList linkRefreshResults() const { return m_openLinks; }

    // ---- import (base_service_import.cpp) ----
    // A proposal is {ok, plan, header, preview (first rows), rowCount} plus
    // the detected delimiter (CSV) or {sheets, sheet} (Excel); the import
    // runs the plan the dialog edited as one undo step.
    Q_INVOKABLE QVariantMap csvProposal(const QUrl &url, const QString &table, const QString &delimiter,
                                        bool headerRow);
    Q_INVOKABLE QVariantMap importCsv(const QUrl &url, const QVariantMap &plan, const QString &delimiter,
                                      bool headerRow);
    // `sheet` "" = the first sheet holding data (base_xlsx_import.h).
    Q_INVOKABLE QVariantMap xlsxProposal(const QUrl &url, const QString &sheet, const QString &table, bool headerRow);
    Q_INVOKABLE QVariantMap importXlsx(const QUrl &url, const QVariantMap &plan, const QString &sheet, bool headerRow);
    Q_INVOKABLE QVariantMap accessTools() const;
    Q_INVOKABLE QVariantMap accessTables(const QUrl &url) const;
    // The chosen tables with their design, the relationships between them
    // and (with `queries`) the select queries — one undo step. The result
    // is the import report (AccessImportReport::toMap, base_access_import.h).
    Q_INVOKABLE QVariantMap importAccess(const QUrl &url, const QStringList &tables, bool queries = true);

    // ---- export, maintenance (base_service_io.cpp) ----
    Q_INVOKABLE QVariantMap exportObject(const QString &kind, const QString &name, const QUrl &url,
                                         const QString &format);
    Q_INVOKABLE QVariantMap compactAndRepair();
    Q_INVOKABLE QVariantMap checkIntegrity() const;
    Q_INVOKABLE QVariantMap backupNow();

    // ---- file fields (base_service_files.cpp, base_file_field.h) ----
    // {ok, hasFile, kind, label, size, sizeText, image, previewUrl,
    // suggestedName}; a picture Qt can decode gets a preview file in the
    // scratch folder (named by its content, so a changed file is a new URL).
    Q_INVOKABLE QVariantMap fileInfo(const QString &table, qint64 rowid, const QString &field) const;
    // Each a single undo step.
    Q_INVOKABLE QVariantMap insertFile(const QString &table, qint64 rowid, const QString &field, const QUrl &url);
    Q_INVOKABLE QVariantMap removeFile(const QString &table, qint64 rowid, const QString &field);
    Q_INVOKABLE QVariantMap saveFile(const QString &table, qint64 rowid, const QString &field, const QUrl &url) const;

    // Scratch folder for presentation databases and design files.
    [[nodiscard]] QString scratchPath(const QString &name) const;

signals:
    void fileChanged();
    // M86: a database file was opened (BaseDocument::opened) — AutoExec.
    void databaseOpened();
    void objectsChanged();
    // Any records changed (edits, imports, undo) — views showing one
    // record's files reload on it.
    void recordsChanged();
    // A toast: variant "info" | "success" | "warning" | "danger".
    void notify(const QString &title, const QString &text, const QString &variant);

private:
    void bindFile();
    // M86: linked tables are refreshed when a database opens (not an undo step).
    void refreshLinksAtOpen();
    // M80: a make-table query's plan for runQuery to show (query tools).
    [[nodiscard]] QVariantMap makeTablePlan(const QindaOffice::Base::QueryDesign &design, const QVariantMap &parameters);
    QVariantMap importTable(const QindaOffice::Base::CsvTable &table, const QVariantMap &plan, const QString &source);
    [[nodiscard]] QVariantMap needsSystemTables() const;

    QPointer<BaseDocument> m_document;
    DatasheetModel *m_datasheet = nullptr;
    QList<DatasheetModel *> m_subforms;  // M80: subformSheet(), created on demand
    LabelSheet *m_labels = nullptr;      // M80
    // M80: the records the open report reads (openReport), so a design edit
    // re-evaluates without asking for the parameters again.
    std::shared_ptr<QindaOffice::Base::ReportSource> m_reportSource;
    QVariantList m_openLinks; // M86: the refresh at open (linkRefreshResults)
    QTemporaryDir m_scratch;
};

} // namespace QindaOffice::Apps::Base
