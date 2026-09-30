// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "agent_provider.h"
namespace QindaOffice::Apps::Notes {
class NoteHost;
class NoteTools;
// Shared by local RPC validation and the built-in assistant. Atomic batch;
// block indices refer to the revision supplied with the request.
QJsonObject noteEditsSchema();
class NoteAgent final : public QindaTK::AgentProvider {
public:
    NoteAgent(NoteHost *host, NoteTools *tools);
    QJsonObject state() const override;
    QJsonArray actions() const override;
    QJsonObject execute(const QString &action, const QJsonObject &arguments) override;
    QString blockedReason() const;
    QString undoRevisionToken() const;
private:
    NoteHost *m_host;
    NoteTools *m_tools;
    QString m_undoRef, m_undoRevision;
};
}
