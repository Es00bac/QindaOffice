// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_document.h"

#include "atomic_file_store.h"
#include "mspdi.h"
#include "plan_file.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QFile>
#include <QFileInfo>
#include <QUndoCommand>
#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

// A whole-project snapshot step (see plan_document.h).
class SnapshotCommand final : public QUndoCommand {
public:
    SnapshotCommand(PlanDocument *document, const QString &label, Engine::Project before, Engine::Project after)
        : QUndoCommand(label)
        , m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void redo() override { m_document->setProject(m_after); }
    void undo() override { m_document->setProject(m_before); }

private:
    PlanDocument *m_document;
    Engine::Project m_before;
    Engine::Project m_after;
};

namespace {
bool isMspdiPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0;
}
} // namespace

PlanDocument::PlanDocument(QObject *parent)
    : Core::Document(parent)
{
    m_undoStack->setUndoLimit(100);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) { setDirty(!clean); });
    reset(QDate::currentDate());
}

void PlanDocument::setProject(const Engine::Project &project)
{
    m_project = project;
    emit projectChanged();
    notifyContentChanged();
}

void PlanDocument::reset(const QDate &start, const QString &title)
{
    // Start on the next working day at the default start time.
    QDate day = start;
    while (day.dayOfWeek() > 5) {
        day = day.addDays(1);
    }
    Engine::Project p = Engine::newProject(day, title);
    Engine::schedule(p);
    m_undoStack->clear();
    m_warnings.clear();
    m_project = p;
    setDirty(false);
    emit projectChanged();
}

bool PlanDocument::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    Engine::Project loaded;
    QStringList warnings;
    if (isMspdiPath(path) || Engine::looksLikeMspdi(bytes.left(512))) {
        const Engine::MspdiResult r = Engine::readMspdi(bytes, &loaded);
        if (!r.ok) {
            if (error) {
                *error = r.error;
            }
            return false;
        }
        warnings = r.warnings;
    } else {
        const Engine::LoadResult r = Engine::readPlanJson(bytes, &loaded);
        if (!r.ok) {
            if (error) {
                *error = r.error;
            }
            return false;
        }
        warnings = r.warnings;
    }
    m_undoStack->clear();
    m_project = loaded;
    m_warnings = warnings;
    setCanonicalPath(path);
    refreshByteRevision();
    m_undoStack->setClean();
    setDirty(false);
    emit projectChanged();
    return true;
}

bool PlanDocument::save(const QString &path, QString *error)
{
    const QByteArray bytes = isMspdiPath(path) ? Engine::writeMspdi(m_project) : Engine::writePlanJson(m_project);
    const Core::AtomicFileStore::Result r = Core::AtomicFileStore::write(path, bytes);
    if (!r.ok) {
        if (error) {
            *error = r.error;
        }
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    m_undoStack->setClean();
    setDirty(false);
    return true;
}

QStringList PlanDocument::mimeTypes() const
{
    return {QString::fromLatin1(kMimeType), QString::fromLatin1(kMspdiMimeType)};
}

QByteArray PlanDocument::recoverySnapshot() const
{
    return Engine::writePlanJson(m_project);
}

bool PlanDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Engine::Project restored;
    const Engine::LoadResult r = Engine::readPlanJson(snapshot, &restored);
    if (!r.ok) {
        if (error) {
            *error = r.error;
        }
        return false;
    }
    m_undoStack->clear();
    m_project = restored;
    setDirty(true);
    emit projectChanged();
    return true;
}

QString PlanDocument::apply(const QString &label, const Operation &operation)
{
    Engine::Project after = m_project;
    const QString refused = operation(after);
    if (!refused.isEmpty()) {
        return refused;
    }
    Engine::schedule(after);
    // AGENT-GUARD: push() runs redo(), which installs `after` and emits
    // projectChanged — never assign m_project before pushing.
    m_undoStack->push(new SnapshotCommand(this, label, m_project, std::move(after)));
    return {};
}

void PlanDocument::replace(const QString &label, Engine::Project project)
{
    Engine::schedule(project);
    m_undoStack->push(new SnapshotCommand(this, label, m_project, std::move(project)));
}

} // namespace QindaOffice::Apps::Plan
