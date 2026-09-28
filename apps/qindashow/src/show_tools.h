// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The assistant's hands in a QindaShow deck (D-058, office_ai
// document_tools.h). The model sees every slide — its text boxes as
// [slide.box] (the first box of a titled layout is what the audience reads
// as the title) and its speaker notes — and rewrites box text, adds,
// deletes and moves slides, and writes notes. One reply's operations are
// ONE SlideListCommand (the same whole-deck command Replace All uses), so
// one Undo reverts them, notes included.

#include "document_tools.h"

#include <QStringList>

namespace QindaOffice::Apps::Show {

class ShowHost;

class ShowTools final : public Ai::DocumentTools {
public:
    explicit ShowTools(ShowHost *host) : m_host(host) {}

    [[nodiscard]] QString documentNoun() const override { return QStringLiteral("presentation"); }
    [[nodiscard]] QString snapshot(int budget) override;
    [[nodiscard]] QString operationGuide() const override;
    Ai::EditOutcome apply(const QJsonArray &edits) override;
    void undo() override;

private:
    ShowHost *m_host;
    QStringList m_slideIds; // snapshot slide number n → id (n is 1-based)
};

} // namespace QindaOffice::Apps::Show
