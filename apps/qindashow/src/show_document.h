// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow document (plan section 6.3; M62, D-070): a
// show_model::Presentation behind the Core::Document contract. PowerPoint
// (.pptx, and .pptm/.potx/.ppsx) opens and saves natively; .fodp is native
// too; .ppt and .odp go through the headless LibreOffice bridge with .pptx
// as the intermediate, both ways. Crash-recovery snapshots are .pptx (the
// format that carries the whole model); journals written before M62 hold
// .fodp and still load.
//
// AGENT-GUARD: LibreOffice intermediates are only ever created inside the
// private converter directory; a user's input file is never removed.

#include "document.h"
#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

class ShowDocument final : public Core::Document {
    Q_OBJECT
public:
    explicit ShowDocument(QObject *parent = nullptr);

    [[nodiscard]] ShowModel::Presentation *presentation() { return &m_presentation; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;
    // The journal holds a .pptx with its pictures: the binary bound.
    [[nodiscard]] qint64 recoveryPayloadLimit() const override;

signals:
    // Raised after a successful load so the window can rebuild canvas,
    // sorter and notes (the shell's loadPath has no app hook).
    void loaded();

private:
    ShowModel::Presentation m_presentation;
};

} // namespace QindaOffice::Apps::Show
