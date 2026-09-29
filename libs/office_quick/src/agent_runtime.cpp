// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent_runtime.h"
#include "document_host.h"
#include "office_application.h"
#include "standard_actions.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
namespace QindaOffice::Quick {
AgentRuntime::AgentRuntime(OfficeApplication *office, int access)
    : QObject(office), m_server(&m_host), m_initialAccess(access) {
  connect(office, &OfficeApplication::hostCreated, this,
          [this](DocumentHost *host) { attach(host); });
}
bool AgentRuntime::start(QString *error) {
  const auto directory = QindaTK::AgentServer::runtimeDirectory();
  if (!QDir(directory).exists()) {
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner |
                                              QFile::ExeOwner)) {
      if (error)
        *error = QStringLiteral("Cannot create private local-agent directory");
      return false;
    }
  }
  const auto path = directory + QLatin1Char('/') +
                    QCoreApplication::applicationName() + QLatin1Char('-') +
                    QString::number(QCoreApplication::applicationPid()) +
                    QStringLiteral(".sock");
  if (!m_server.listen(path, error))
    return false;
  m_available = true;
  qInfo("QindaOffice local agent endpoint: %s", qPrintable(path));
  return true;
}
void AgentRuntime::attach(DocumentHost *host) {
  auto *provider = host->agentProvider();
  if (!provider)
    return;
  if (!m_available) {
    for (const char *id : {ActionId::kAgentDisconnect, ActionId::kAgentRead,
                           ActionId::kAgentEdit})
      host->action(QString::fromLatin1(id))->setEnabled(false);
    host->showStatusMessage(
        tr("Local agent service is unavailable; access remains disconnected"));
    return;
  }
  m_host.addProvider(provider, QindaTK::AgentHost::Grant::Off);
  connect(host, &DocumentHost::agentAccessChanged, this,
          [this, host, id = provider->id()] {
            m_host.setGrant(id, static_cast<QindaTK::AgentHost::Grant>(
                                    host->agentAccess()));
          });
  connect(&m_host, &QindaTK::AgentHost::event, host,
          [host, id = provider->id()](const QJsonObject &event) {
            if (event.value(QStringLiteral("document_id")).toString() == id &&
                event.value(QStringLiteral("kind")).toString() ==
                    QLatin1String("action_finished"))
              host->showStatusMessage(
                  QObject::tr("Local agent action finished — review the "
                              "document and its undo history"));
          });
  host->setAgentAccess(m_initialAccess);
}
} // namespace QindaOffice::Quick
