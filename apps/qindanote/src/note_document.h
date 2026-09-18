// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The notebook as a Core::Document (plan section 6.4): one notebook
// directory = one document = one window. Pages load lazily; persistence
// goes through Notebook (notebook.h). Unchanged by the QtQuick port —
// only its window moved (note_host.h).

#include "notebook.h"

#include "document.h"

namespace QindaOffice::Apps::Notes {

class NoteDocument final : public Core::Document {
    Q_OBJECT
public:
    NoteDocument();
    [[nodiscard]] Notebook *notebook() { return &m_notebook; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override
    {
        return m_notebook.toRecoveryJson();
    }
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

private:
    Notebook m_notebook;
};

} // namespace QindaOffice::Apps::Notes
