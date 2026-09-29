// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "agent_provider.h"
namespace QindaOffice::Ai {
class DocumentTools;
}
namespace QindaOffice::Apps::QindaCalc {
class CalcHost;
class SpreadsheetDocument;
// GUI-thread provider owned by CalcHost. Document and tools are borrowed
// from that host and must remain alive until the provider is destroyed.
class CalcAgent final : public QindaTK::AgentProvider {
public:
  CalcAgent(CalcHost *host, SpreadsheetDocument *document,
            Ai::DocumentTools *tools);
  QJsonObject state() const override;
  QJsonArray actions() const override;
  QJsonObject execute(const QString &action,
                      const QJsonObject &arguments) override;
  QString blockedReason() const;
  QString undoRevisionToken() const;

private:
  CalcHost *m_host;
  SpreadsheetDocument *m_document;
  Ai::DocumentTools *m_tools;
  QString m_undoRef;
  quint64 m_undoGeneration = 0;
  int m_undoIndex = -1;
};
} // namespace QindaOffice::Apps::QindaCalc
