// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// InlineActionFlow (plan section 5.3): Rewrite / Shorten / Expand / Fix
// grammar / Summarize / Continue writing / Explain over the current
// selection, with a ProposalDialog showing original vs proposal and
// Apply / Regenerate / Discard. Structured output v1 is "text in a fenced
// block" extraction everywhere.

#include "assistant_panel.h"
#include "document_context.h"

#include <QDialog>

class QTextEdit;

namespace QindaOffice::Ai {

// One canned inline action -> prompt template.
struct InlineAction {
    QString id;
    QString displayName;
    QString prompt;
};

[[nodiscard]] const QVector<InlineAction> &inlineActions();

class ProposalDialog : public QDialog {
    Q_OBJECT
public:
    ProposalDialog(const QString &original, const QString &proposal, QWidget *parent = nullptr);

signals:
    void regenerateRequested();

private:
    QTextEdit *m_original;
    QTextEdit *m_proposal;
};

class InlineActionFlow : public QObject {
    Q_OBJECT
public:
    InlineActionFlow(ModelCatalog *catalog, DocumentContextProvider *context,
                     EditApplier *applier, QObject *parent = nullptr);

    // Runs the action over the current selection (explicit user action;
    // nothing is sent without this call).
    void run(const QString &actionId, QWidget *dialogParent);

private:
    void execute(const InlineAction &action);
    [[nodiscard]] static QString extractProposal(const QString &modelText);

    ModelCatalog *m_catalog;
    DocumentContextProvider *m_context;
    EditApplier *m_applier;
    InlineAction m_pending;
};

} // namespace QindaOffice::Ai
