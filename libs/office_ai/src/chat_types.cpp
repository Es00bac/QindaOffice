// SPDX-License-Identifier: GPL-3.0-or-later
#include "chat_types.h"

namespace QindaOffice::Ai {

bool ChatRun::emitFinished(const ChatResult &result)
{
    if (m_terminalEmitted) {
        return false;
    }
    m_terminalEmitted = true;
    emit finished(result);
    return true;
}

bool ChatRun::emitFailed(const ChatError &error)
{
    if (m_terminalEmitted) {
        return false;
    }
    m_terminalEmitted = true;
    emit failed(error);
    return true;
}

} // namespace QindaOffice::Ai
