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
// base_service_queries.cpp (queries), base_service_forms.cpp (forms and
// reports), base_service_io.cpp (import/export, maintenance).

#include "datasheet_model.h"

#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Base {

class BaseDocument;

class BaseService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by BaseHost.")
    Q_PROPERTY(bool hasDatabase READ hasDatabase NOTIFY fileChanged)
    Q_PROPERTY(QString databaseName READ databaseName NOTIFY fileChanged)
    Q_PROPERTY(QString workingPath READ workingPath NOTIFY fileChanged)
    Q_PROPERTY(bool hasSystemTables READ hasSystemTables NOTIFY objectsChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY fileChanged)
    Q_PROPERTY(QStringList openProblems READ openProblems NOTIFY fileChanged)
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)
    Q_PROPERTY(QStringList tableNames READ tableNames NOTIFY objectsChanged)
    Q_PROPERTY(QStringList queryNames READ queryNames NOTIFY objectsChanged)
    Q_PROPERTY(QindaOffice::Apps::Base::DatasheetModel *datasheet READ datasheet CONSTANT)
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    Q_PROPERTY(QVariantList fieldTypes READ fieldTypes CONSTANT)
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
    [[nodiscard]] DatasheetModel *datasheet() const { return m_datasheet; }
    [[nodiscard]] QVariantList templates() const;
    [[nodiscard]] QVariantList fieldTypes() const;
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

    // ---- import, export, maintenance (base_service_io.cpp) ----
    Q_INVOKABLE QVariantMap csvProposal(const QUrl &url, const QString &table, const QString &delimiter,
                                        bool headerRow);
    Q_INVOKABLE QVariantMap importCsv(const QUrl &url, const QVariantMap &plan, const QString &delimiter,
                                      bool headerRow);
    Q_INVOKABLE QVariantMap exportObject(const QString &kind, const QString &name, const QUrl &url,
                                         const QString &format);
    Q_INVOKABLE QVariantMap accessTools() const;
    Q_INVOKABLE QVariantMap accessTables(const QUrl &url) const;
    Q_INVOKABLE QVariantMap importAccess(const QUrl &url, const QStringList &tables);
    Q_INVOKABLE QVariantMap compactAndRepair();
    Q_INVOKABLE QVariantMap checkIntegrity() const;
    Q_INVOKABLE QVariantMap backupNow();

    // Scratch folder for presentation databases and design files.
    [[nodiscard]] QString scratchPath(const QString &name) const;

signals:
    void fileChanged();
    void objectsChanged();
    // A toast: variant "info" | "success" | "warning" | "danger".
    void notify(const QString &title, const QString &text, const QString &variant);

private:
    void bindFile();
    [[nodiscard]] QVariantMap needsSystemTables() const;

    QPointer<BaseDocument> m_document;
    DatasheetModel *m_datasheet = nullptr;
    QTemporaryDir m_scratch;
};

} // namespace QindaOffice::Apps::Base
