// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// InlineActionFlow (plan section 5.3): Rewrite / Shorten / Expand / Fix
// grammar / Summarize / Continue writing / Explain over the current
// selection. The model's proposal is shown against the original with
// Apply / Regenerate / Discard through a PRESENTER the app supplies
// (conversion plan §3.8: the QML window fills it with ProposalDialog.qml;
// with no presenter the `proposalReady` signal is all there is). Structured
// output v1 is "text in a fenced block" extraction everywhere.

#include "document_context.h"

#include <QObject>
#include <QVector>

#include <functional>

namespace QindaOffice::Ai {

class ModelCatalog;

// One canned inline action -> prompt template.
struct InlineAction {
    QString id;
    QString displayName;
    QString prompt;
};

[[nodiscard]] const QVector<InlineAction> &inlineActions();

// How a proposal reaches the user. `show` presents original vs proposal
// and calls exactly one of apply() / regenerate() / nothing (discard);
// `failed` reports a provider error.
struct ProposalPresenter {
    std::function<void(const QString &original, const QString &proposal,
                       std::function<void()> apply, std::function<void()> regenerate)>
        show;
    std::function<void(const QString &message)> failed;
};

class InlineActionFlow : public QObject {
    Q_OBJECT
public:
    InlineActionFlow(ModelCatalog *catalog, DocumentContextProvider *context,
                     EditApplier *applier, QObject *parent = nullptr);

    void setPresenter(ProposalPresenter presenter) { m_presenter = std::move(presenter); }

    // Runs the action over the current selection (explicit user action;
    // nothing is sent without this call).
    void run(const QString &actionId);

    [[nodiscard]] static QString extractProposal(const QString &modelText);

signals:
    // Mirrors of the presenter calls, for tests and status lines.
    void proposalReady(const QString &original, const QString &proposal);
    void failed(const QString &message);

private:
    void execute(const InlineAction &action);
    void present(const InlineAction &action, const QString &original, const QString &proposal);

    ModelCatalog *m_catalog;
    DocumentContextProvider *m_context;
    EditApplier *m_applier;
    InlineAction m_pending;
    ProposalPresenter m_presenter;
};

} // namespace QindaOffice::Ai
