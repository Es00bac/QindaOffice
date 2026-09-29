// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "agent_host.h"
#include "agent_server.h"
namespace QindaOffice::Quick {
class DocumentHost;
class OfficeApplication;
// Owned by OfficeApplication on the GUI thread; the host borrows document
// providers. Grants live only in this process and are never persisted.
class AgentRuntime : public QObject {
public:
  AgentRuntime(OfficeApplication *office, int initialAccess);
  bool start(QString *error);

private:
  void attach(DocumentHost *host);
  QindaTK::AgentHost m_host;
  QindaTK::AgentServer m_server;
  int m_initialAccess;
  bool m_available = false;
};
} // namespace QindaOffice::Quick
