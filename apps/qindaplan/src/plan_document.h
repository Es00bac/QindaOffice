// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanDocument: one project (a .qplan file, or a Microsoft Project XML file
// kept as MSPDI) behind the Core::Document contract (rule 1: one project =
// one window). The engine's Project is a value; every edit replaces it.
//
// AGENT-CONTRACT (undo, D-051): an edit is `apply(label, operation)`: the
// operation runs on a COPY of the project; when it succeeds the copy is
// scheduled and becomes the project through a snapshot QUndoCommand
// holding the before and after values (a 2,000-task plan is a few hundred
// KB, so 100 undo steps stay bounded). A refused operation changes
// nothing and pushes nothing. Undo/redo swap whole snapshots, so they can
// never disagree with the scheduler. The undo stack's clean state is the
// saved state (dirty = !clean).
//
// Formats: the path's suffix picks it — ".xml" reads and writes MSPDI
// (plan_engine/mspdi.h), anything else is the native JSON (plan_file.h).
// The recovery journal stores the native JSON.

#include "document.h"
#include "plan_types.h"

#include <QDate>

#include <functional>

namespace QindaOffice::Apps::Plan {

namespace Engine = ::QindaOffice::Plan;

class PlanDocument final : public Core::Document {
    Q_OBJECT
public:
    static constexpr const char *kMimeType = "application/x-qindaplan";
    // Microsoft Project XML has no registered type of its own; it is XML.
    static constexpr const char *kMspdiMimeType = "application/xml";

    explicit PlanDocument(QObject *parent = nullptr);

    [[nodiscard]] const Engine::Project &project() const { return m_project; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // One undoable edit (see the contract above). The operation returns an
    // empty string on success, else the reason it refused.
    using Operation = std::function<QString(Engine::Project &)>;
    QString apply(const QString &label, const Operation &operation);
    // Replaces the whole project as one undoable step (import, paste of a
    // template); the new project is scheduled first.
    void replace(const QString &label, Engine::Project project);
    // A new project starting on `start` (File ▸ New; no undo, not dirty).
    void reset(const QDate &start, const QString &title = {});
    // Load messages from the last load (MSPDI and repair warnings).
    [[nodiscard]] QStringList loadWarnings() const { return m_warnings; }

signals:
    // The project changed (edit, undo, redo, load, reset).
    void projectChanged();

private:
    friend class SnapshotCommand;
    void setProject(const Engine::Project &project);

    Engine::Project m_project;
    QStringList m_warnings;
};

} // namespace QindaOffice::Apps::Plan
