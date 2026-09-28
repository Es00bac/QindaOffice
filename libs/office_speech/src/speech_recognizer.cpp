// SPDX-License-Identifier: GPL-3.0-or-later
#include "speech_recognizer.h"

namespace QindaOffice::Speech {

QString SpeechRecognizer::clipPrompt(const QString &previous) const
{
    QString prompt;
    if (!m_keyterms.isEmpty()) {
        prompt = QStringLiteral("Glossary: %1. ").arg(m_keyterms.join(QStringLiteral(", ")));
    }
    return prompt + previous.right(200);
}

void SpeechRecognizer::emitFinishedOnce()
{
    if (m_finishedEmitted) {
        return;
    }
    m_finishedEmitted = true;
    emit finished();
}

} // namespace QindaOffice::Speech
