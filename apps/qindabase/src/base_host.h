// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BaseHost: one QindaBase window = one database file (M49, D-049; rule 1).
// The host owns the BaseDocument (base_document.h), the window's
// BaseService (`base`), the commands and menus, the view state the ribbon's
// contextual tabs follow, and the one print source (a report's paginated
// document from QindaTK) that Print, Print Preview and Export PDF render.
//
// AGENT-CONTRACT (with qml/BaseWindow.qml and friends):
//  * Commands are ActionEntries with the app-scoped ids in
//    base_host_actions.cpp (rule 5 — never rename one). A command the QML
//    answers emits actionRequested(id); the window's Connections switch on
//    the id. The host never opens UI itself.
//  * `currentView` names what the main area shows: "start" | "datasheet" |
//    "tableDesign" | "queryDesign" | "form" | "report" | "relationships" |
//    "sql" | "macroDesign" (M80); `currentKind`/`currentName` name the
//    object. The QML sets them; the host derives enablement and the
//    contextual ribbon tabs ("tableDesign", "queryDesign", "formDesign",
//    "macroDesign") from them.
//  * `selectedKind`/`selectedName` are the navigation pane's selection
//    (Open, Design, Rename, Delete and the Create commands act on it).
//  * `designDirty`: the open designer has unsaved edits. It makes the
//    document dirty (so closing asks), and Save (Ctrl+S) — or Save in the
//    close consent — asks the QML to save the design (designSaveRequested)
//    before the document's own no-op save.
//  * `designHistory` true routes Edit ▸ Undo/Redo to the designer's own
//    history (the QML answers actionRequested("editUndoAction")).
//  * `fileTarget` is the file field the File commands act on (M65): the
//    datasheet's current cell or the form's chosen file field, as
//    {table, rowid, field, hasFile}; empty when there is none.

#include "base_service.h"
#include "document_host.h"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class RichTextDocument;
}

namespace QindaOffice::Apps::Base {

class BaseDocument;
class LabelSheet;

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class BaseHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QindaOffice::Apps::Base::BaseService *base READ base CONSTANT)
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY viewChanged)
    Q_PROPERTY(QString currentKind READ currentKind WRITE setCurrentKind NOTIFY viewChanged)
    Q_PROPERTY(QString currentName READ currentName WRITE setCurrentName NOTIFY viewChanged)
    Q_PROPERTY(QString selectedKind READ selectedKind WRITE setSelectedKind NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName WRITE setSelectedName NOTIFY selectionChanged)
    Q_PROPERTY(bool designDirty READ designDirty WRITE setDesignDirty NOTIFY designChanged)
    Q_PROPERTY(bool designHistory READ designHistory WRITE setDesignHistory NOTIFY designChanged)
    Q_PROPERTY(bool designCanUndo READ designCanUndo WRITE setDesignCanUndo NOTIFY designChanged)
    Q_PROPERTY(bool designCanRedo READ designCanRedo WRITE setDesignCanRedo NOTIFY designChanged)
    Q_PROPERTY(QVariantMap fileTarget READ fileTarget WRITE setFileTarget NOTIFY fileTargetChanged)
    Q_PROPERTY(bool navigationVisible READ navigationVisible NOTIFY navigationChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(bool printable READ printable NOTIFY printSourceChanged)
public:
    BaseHost();
    ~BaseHost() override;

    [[nodiscard]] BaseService *base() const { return m_service; }
    [[nodiscard]] BaseDocument *baseDocument() const { return m_document; }
    [[nodiscard]] QString currentView() const { return m_view; }
    void setCurrentView(const QString &view);
    [[nodiscard]] QString currentKind() const { return m_kind; }
    void setCurrentKind(const QString &kind);
    [[nodiscard]] QString currentName() const { return m_name; }
    void setCurrentName(const QString &name);
    [[nodiscard]] QString selectedKind() const { return m_selectedKind; }
    void setSelectedKind(const QString &kind);
    [[nodiscard]] QString selectedName() const { return m_selectedName; }
    void setSelectedName(const QString &name);
    [[nodiscard]] bool designDirty() const { return m_designDirty; }
    void setDesignDirty(bool dirty);
    [[nodiscard]] bool designHistory() const { return m_designHistory; }
    void setDesignHistory(bool active);
    [[nodiscard]] bool designCanUndo() const { return m_designCanUndo; }
    void setDesignCanUndo(bool can);
    [[nodiscard]] bool designCanRedo() const { return m_designCanRedo; }
    void setDesignCanRedo(bool can);
    [[nodiscard]] QVariantMap fileTarget() const { return m_fileTarget; }
    void setFileTarget(const QVariantMap &target);
    [[nodiscard]] bool navigationVisible() const { return m_navigationVisible; }
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    [[nodiscard]] bool printable() const { return !m_report.isNull() || !m_labels.isNull(); }

    // What the report view shows now (null: nothing to print): the pages of
    // a report (a QindaTK RichTextDocument, M80: built by the engine,
    // BaseService::buildReport) or a label sheet (label_sheet.h).
    Q_INVOKABLE void setReportOutput(QObject *output);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    [[nodiscard]] Q_INVOKABLE QString clipboardText() const;
    // Export PDF… of the report shown (the same pages Print renders).
    Q_INVOKABLE QVariantMap exportReportPdf(const QUrl &url);
    // Checks a command's radio state from QML (query type, workspace).
    Q_INVOKABLE void setChecked(const QString &id, bool checked);

signals:
    void viewChanged();
    void selectionChanged();
    void designChanged();
    void navigationChanged();
    void fileTargetChanged();
    void zoomChanged();
    void printSourceChanged();
    void actionRequested(const QString &id);
    void designSaveRequested();

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;
    // Labels print on their sheet's paper, edge to edge (label_sheet.h); a
    // report on its own paper and orientation (M80).
    [[nodiscard]] std::optional<QPageLayout> printPageLayout() const override;
    [[nodiscard]] bool printPrefersLandscape() const override;

private:
    // base_host_actions.cpp
    void createActions();
    void insertMenus();
    void syncEnablement();
    void updateStatus();

    BaseDocument *m_document = nullptr;
    BaseService *m_service = nullptr;
    QPointer<QindaTK::RichTextDocument> m_report;
    QPointer<LabelSheet> m_labels;
    QString m_view = QStringLiteral("start");
    QString m_kind;
    QString m_name;
    QString m_selectedKind;
    QString m_selectedName;
    QVariantMap m_fileTarget;
    qreal m_zoom = 1.0;
    bool m_designDirty = false;
    bool m_designHistory = false;
    bool m_designCanUndo = false;
    bool m_designCanRedo = false;
    bool m_navigationVisible = true;
};

} // namespace QindaOffice::Apps::Base
