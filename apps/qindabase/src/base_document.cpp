// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_document.h"

#include "base_templates.h"

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUndoCommand>
#include <QUndoStack>

#include <filesystem>
#include <system_error>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

// One Change as an undo step. The first redo() is skipped: the change was
// applied when it was made.
class ChangeCommand final : public QUndoCommand {
public:
    ChangeCommand(BaseDocument *document, Engine::Change change, QUndoCommand *parent = nullptr)
        : QUndoCommand(change.label, parent)
        , m_document(document)
        , m_change(std::move(change))
    {
    }
    void undo() override { replay(true); }
    void redo() override
    {
        if (m_fresh) {
            m_fresh = false;
            return;
        }
        replay(false);
    }

private:
    void replay(bool backwards)
    {
        if (m_document == nullptr || m_document->file() == nullptr) {
            return;
        }
        const Engine::Result r = m_document->file()->applyChange(m_change, backwards);
        if (!r.ok) {
            m_document->replayFailed(r.error);
        }
    }
    QPointer<BaseDocument> m_document;
    Engine::Change m_change;
    bool m_fresh = true;
};

// Several changes as one step (children replay in order, undo in reverse).
class MacroCommand final : public QUndoCommand {
public:
    explicit MacroCommand(const QString &label)
        : QUndoCommand(label)
    {
    }
    void redo() override
    {
        if (m_fresh) {
            m_fresh = false;
            return;
        }
        QUndoCommand::redo();
    }

private:
    bool m_fresh = true;
};

BaseDocument::BaseDocument(QObject *parent)
    : Core::Document(parent)
{
}

BaseDocument::~BaseDocument() = default;

void BaseDocument::adopt(std::unique_ptr<Engine::BaseFile> file, const QString &path, bool untitled)
{
    m_file = std::move(file);
    m_undoStack->clear();
    setCanonicalPath(untitled ? QString() : path);
    refreshByteRevision();
    setDirty(untitled);
    // Our own commits must never look like another program's edits to the
    // host's "changed on disk" check.
    connect(m_file.get(), &Engine::BaseFile::dataChanged, this, [this] { refreshByteRevision(); });
    connect(m_file.get(), &Engine::BaseFile::schemaChanged, this, [this] { refreshByteRevision(); });
    connect(m_file.get(), &Engine::BaseFile::objectsChanged, this, [this] { refreshByteRevision(); });
    emit fileReplaced();
}

bool BaseDocument::load(const QString &path, QString *error)
{
    std::unique_ptr<Engine::BaseFile> file = Engine::BaseFile::open(path, error);
    if (!file) {
        return false;
    }
    m_file.reset();
    m_scratch.reset();
    adopt(std::move(file), QFileInfo(path).absoluteFilePath(), false);
    emit opened();
    return true;
}

bool BaseDocument::createAt(const QString &path, const QString &templateId, QString *error)
{
    std::unique_ptr<Engine::BaseFile> file = Engine::BaseFile::create(path, error);
    if (!file) {
        return false;
    }
    if (!templateId.isEmpty()) {
        const Engine::Result r = Engine::applyTemplate(*file, templateId);
        if (!r.ok) {
            if (error != nullptr) {
                *error = r.error;
            }
            file.reset();
            QFile::remove(path);
            return false;
        }
    }
    m_file.reset();
    m_scratch.reset();
    adopt(std::move(file), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool BaseDocument::createUntitled(const QString &templateId, QString *error)
{
    auto scratch = std::make_unique<QTemporaryDir>();
    if (!scratch->isValid()) {
        if (error != nullptr) {
            *error = scratch->errorString();
        }
        return false;
    }
    std::unique_ptr<Engine::BaseFile> file =
        Engine::BaseFile::create(scratch->filePath(QStringLiteral("untitled.qbase")), error);
    if (!file) {
        return false;
    }
    if (!templateId.isEmpty()) {
        const Engine::Result r = Engine::applyTemplate(*file, templateId);
        if (!r.ok) {
            if (error != nullptr) {
                *error = r.error;
            }
            return false;
        }
    }
    m_file.reset(); // close before the old scratch dir goes away
    m_scratch = std::move(scratch);
    adopt(std::move(file), QString(), true);
    return true;
}

bool BaseDocument::save(const QString &path, QString *error)
{
    if (!m_file) {
        if (error != nullptr) {
            *error = tr("No database is open.");
        }
        return false;
    }
    if (m_designPending && m_designFlush && !m_designFlush(error)) {
        return false;
    }
    const QString target = QFileInfo(path).absoluteFilePath();
    if (!isUntitled() && target == QFileInfo(m_file->path()).absoluteFilePath()) {
        refreshByteRevision();
        setDirty(false);
        return true; // everything is committed already
    }
    const QString temp = target + QStringLiteral(".saving");
    QFile::remove(temp);
    const Engine::Result copied = m_file->backupTo(temp);
    if (!copied.ok) {
        if (error != nullptr) {
            *error = copied.error;
        }
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(temp.toStdString(), target.toStdString(), ec);
    if (ec) {
        QFile::remove(temp);
        if (error != nullptr) {
            *error = QString::fromStdString(ec.message());
        }
        return false;
    }
    std::unique_ptr<Engine::BaseFile> reopened = Engine::BaseFile::open(target, error);
    if (!reopened) {
        return false;
    }
    m_file.reset();
    m_scratch.reset(); // an untitled database's scratch file goes with it
    adopt(std::move(reopened), target, false);
    return true;
}

QStringList BaseDocument::mimeTypes() const
{
    return {QString::fromLatin1(kMimeType), QStringLiteral("application/vnd.sqlite3")};
}

QByteArray BaseDocument::recoverySnapshot() const
{
    return {};
}

bool BaseDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(snapshot);
    if (error != nullptr) {
        *error = tr("Databases are saved as you work; there is nothing to recover.");
    }
    return false;
}

void BaseDocument::pushChange(const Engine::Change &change)
{
    if (change.isEmpty()) {
        return;
    }
    m_undoStack->push(new ChangeCommand(this, change));
}

void BaseDocument::pushChanges(const QString &label, const QList<Engine::Change> &changes)
{
    QList<Engine::Change> real;
    for (const Engine::Change &c : changes) {
        if (!c.isEmpty()) {
            real.append(c);
        }
    }
    if (real.size() == 1) {
        pushChange(real.first());
        return;
    }
    if (real.isEmpty()) {
        return;
    }
    auto *macro = new MacroCommand(label);
    for (const Engine::Change &c : real) {
        auto *child = new ChangeCommand(this, c, macro);
        child->redo(); // consume the "fresh" skip; the macro skips its first redo
    }
    m_undoStack->push(macro);
}

void BaseDocument::setDesignPending(bool pending)
{
    m_designPending = pending;
    setDirty(pending || isUntitled());
}

void BaseDocument::replayFailed(const QString &message)
{
    // AGENT-GUARD: a half-replayed history cannot be trusted; clearing it
    // is the only safe state. Deferred: we are inside QUndoStack::undo().
    QMetaObject::invokeMethod(this, [this, message] {
        m_undoStack->clear();
        emit undoFailed(message);
    }, Qt::QueuedConnection);
}

} // namespace QindaOffice::Apps::Base
