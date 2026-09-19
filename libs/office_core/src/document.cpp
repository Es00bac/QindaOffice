// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include "atomic_file_store.h"

#include <QFileInfo>
#include <QUndoStack>

namespace QindaOffice::Core {

Document::Document(QObject *parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
{
}

Document::~Document() = default;

QString Document::displayTitle() const
{
    if (m_path.isEmpty()) {
        return tr("Untitled");
    }
    const QString name = QFileInfo(m_path).fileName();
    // Bound: display titles stay ≤128 UTF-16 units (plan section 5.1).
    return name.left(128);
}

void Document::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

void Document::adoptPath(const QString &path)
{
    setCanonicalPath(path);
    refreshByteRevision();
}

void Document::refreshByteRevision()
{
    m_byteRevision = m_path.isEmpty() ? 0 : fileRevision(m_path);
}

void Document::notifyContentChanged()
{
    emit contentChanged();
}

bool Document::loadCanRunOnWorker(const QString &path) const
{
    Q_UNUSED(path);
    return false;
}

bool Document::loadStaged(const QString &path, QString *error)
{
    Q_UNUSED(path);
    Q_UNUSED(error);
    return false;
}

void Document::adoptStagedLoad(const QString &path)
{
    Q_UNUSED(path);
}

void Document::setCanonicalPath(const QString &path)
{
    m_path = path;
}

} // namespace QindaOffice::Core
