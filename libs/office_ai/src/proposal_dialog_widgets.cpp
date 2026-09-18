// SPDX-License-Identifier: GPL-3.0-or-later
#include "proposal_dialog_widgets.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace QindaOffice::Ai {

ProposalDialog::ProposalDialog(const QString &original, const QString &proposal, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("AI Proposal"));
    auto *layout = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    m_original = new QTextEdit(this);
    m_original->setPlainText(original);
    m_original->setReadOnly(true);
    m_proposal = new QTextEdit(this);
    m_proposal->setPlainText(proposal);
    columns->addWidget(m_original);
    columns->addWidget(m_proposal);
    layout->addLayout(columns);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *apply = buttons->addButton(QDialogButtonBox::Apply);
    QPushButton *regenerate = buttons->addButton(tr("Regenerate"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Discard);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(regenerate, &QPushButton::clicked, this, [this] {
        emit regenerateRequested();
        // AGENT-GUARD: Regenerate must NOT close as Accepted — the caller
        // applies the proposal on Accepted, which replaced the selection
        // with exactly the text the user asked to regenerate.
        done(QDialog::Rejected);
    });
    connect(apply, &QPushButton::clicked, this, [this] { done(QDialog::Accepted); });
}

ProposalPresenter widgetsProposalPresenter(QObject *dialogParent)
{
    auto *widget = qobject_cast<QWidget *>(dialogParent);
    ProposalPresenter presenter;
    presenter.show = [widget](const QString &original, const QString &proposal,
                              std::function<void()> apply, std::function<void()> regenerate) {
        // AGENT-GUARD: open(), not exec(): a nested event loop outlived the
        // flow once and dereferenced it afterwards (use-after-free).
        auto *dialog = new ProposalDialog(original, proposal, widget);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(dialog, &ProposalDialog::regenerateRequested, dialog,
                         [regenerate] { regenerate(); });
        QObject::connect(dialog, &QDialog::accepted, dialog, [apply] { apply(); });
        dialog->open();
    };
    presenter.failed = [widget](const QString &message) {
        if (widget != nullptr) {
            QMessageBox::warning(widget, QObject::tr("AI action failed"), message);
        }
    };
    return presenter;
}

} // namespace QindaOffice::Ai
