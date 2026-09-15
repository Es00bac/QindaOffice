// SPDX-License-Identifier: GPL-3.0-or-later
#include "plain_text_document.h"

#include "atomic_file_store.h"

#include <QFile>

namespace QindaOffice::Core {

bool PlainTextDocument::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    m_text = QString::fromUtf8(file.readAll());
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

bool PlainTextDocument::save(const QString &path, QString *error)
{
    const AtomicFileStore::Result result = AtomicFileStore::writeText(path, m_text);
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList PlainTextDocument::mimeTypes() const
{
    return {QStringLiteral("text/plain"), QStringLiteral("text/html"),
            QStringLiteral("text/markdown")};
}

QByteArray PlainTextDocument::recoverySnapshot() const
{
    return m_text.toUtf8();
}

bool PlainTextDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(error);
    m_text = QString::fromUtf8(snapshot);
    // A restored journal entry is unsaved by definition.
    setDirty(true);
    return true;
}

void PlainTextDocument::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    setDirty(true);
    notifyContentChanged();
}

} // namespace QindaOffice::Core
