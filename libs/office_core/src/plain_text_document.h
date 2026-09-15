// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document.h"

namespace QindaOffice::Core {

// The M1 stub document: a plain QString payload with UTF-8 on disk. It
// exists so open/save/restore-after-crash can be proven against the shell
// before the real engines land (M2+). The recovery snapshot is the raw text.
class PlainTextDocument : public Document {
    Q_OBJECT
public:
    using Document::Document;

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text);

private:
    QString m_text;
};

} // namespace QindaOffice::Core
