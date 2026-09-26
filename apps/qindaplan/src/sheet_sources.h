// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The task and resource sheets as domain sources of Tk.EditableGrid
// (QindaTK docs/editable-grid.md "Domain sources"): the grid asks for the
// cells it shows, edits start from the engine's edit text, and commits go
// to PlanService as one undo step (sourceCommits = true, so the grid keeps
// no overrides and no history of its own — the document's undo stack owns
// undo).
//
// AGENT-CONTRACT (row and column ids): task rows are task GUID strings,
// resource rows "r<uid>", and both sheets end with the blank row "new" that
// creates a task/resource when a name is typed into it (Project's empty
// rows). Column ids are plan_edit.h field ids.

#include <qindatk/editable_grid_model.h>

namespace QindaOffice::Apps::Plan {

class PlanService;

class TaskSheetSource final : public QindaTK::GridCellSource, public QindaTK::GridCellSourceExtension {
    Q_OBJECT
public:
    explicit TaskSheetSource(PlanService *service);
    [[nodiscard]] QVariant value(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariant editValue(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariantMap cellStyle(const QString &rowId, const QString &columnId) const override;
    bool commitChanges(const QString &label, const QVariantList &changes, QString *error) override;

private:
    PlanService *m_service;
};

class ResourceSheetSource final : public QindaTK::GridCellSource, public QindaTK::GridCellSourceExtension {
    Q_OBJECT
public:
    explicit ResourceSheetSource(PlanService *service);
    [[nodiscard]] QVariant value(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariant editValue(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariantMap cellStyle(const QString &rowId, const QString &columnId) const override;
    bool commitChanges(const QString &label, const QVariantList &changes, QString *error) override;

private:
    PlanService *m_service;
};

} // namespace QindaOffice::Apps::Plan
