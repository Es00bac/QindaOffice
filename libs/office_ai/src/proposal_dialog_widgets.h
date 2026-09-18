// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Qt Widgets proposal dialog, kept for the apps not yet converted to
// QindaTK (conversion plan §6 M27: compiled only while QQO_WIDGETS_APPS is
// on; removed with them in M31). The QML window uses ProposalDialog.qml
// through InlineActionFlow::setPresenter instead.

#include "inline_action_flow.h"

#include <QDialog>

class QTextEdit;

namespace QindaOffice::Ai {

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

// A presenter that opens ProposalDialog over `dialogParent` (a QWidget) and
// reports failures with a QMessageBox.
[[nodiscard]] ProposalPresenter widgetsProposalPresenter(QObject *dialogParent);

} // namespace QindaOffice::Ai
