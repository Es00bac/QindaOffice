// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent_provider.h"
#include "document_host.h"
#include "standard_actions.h"
namespace QindaOffice::Quick {
void DocumentHost::setAgentProvider(QindaTK::AgentProvider *provider) {
  if (m_agentProvider || !provider)
    return;
  m_agentProvider = provider;
  const QStringList ids{QString::fromLatin1(ActionId::kAgentDisconnect),
                        QString::fromLatin1(ActionId::kAgentRead),
                        QString::fromLatin1(ActionId::kAgentEdit)};
  for (int i = 0; i < ids.size(); ++i) {
    auto *entry = action(ids.at(i));
    entry->setEnabled(true);
    connect(entry, &ActionEntry::triggered, this,
            [this, i] { setAgentAccess(i); });
  }
  connect(provider, &QObject::destroyed, this, [this, ids] {
    m_agentProvider = nullptr;
    m_agentAccess = 0;
    for (const QString &id : ids)
      action(id)->setEnabled(false);
    emit agentAccessChanged();
  });
  setAgentAccess(0);
}
void DocumentHost::setAgentAccess(int access) {
  if (!m_agentProvider)
    return;
  access = qBound(0, access, 2);
  const QStringList ids{QString::fromLatin1(ActionId::kAgentDisconnect),
                        QString::fromLatin1(ActionId::kAgentRead),
                        QString::fromLatin1(ActionId::kAgentEdit)};
  for (int i = 0; i < ids.size(); ++i)
    action(ids.at(i))->setChecked(i == access);
  if (m_agentAccess == access)
    return;
  m_agentAccess = access;
  emit agentAccessChanged();
  showStatusMessage(access == 0
                        ? tr("Local agents disconnected from this document")
                    : access == 1 ? tr("Local agents may read this document")
                                  : tr("Local agents may read and edit this "
                                       "document — undo remains available"));
}
} // namespace QindaOffice::Quick
