// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Mixes two 16 kHz mono s16le streams (microphone + desktop audio for a
// call or a lecture you are also speaking in) into one. Pure and
// synchronous so the tests can drive it byte by byte.
//
// The two pw-record processes deliver at their own pace; mixing only what
// BOTH have delivered would stall the whole session when one side goes
// quiet at the PipeWire level (a suspended sink delivers nothing at all).
// So a stream that falls more than kMaxLagMs behind the other is treated
// as silence for the difference.

#include <QByteArray>

namespace QindaOffice::Speech {

class PcmMixer {
public:
    static constexpr int kMaxLagMs = 300;

    // Appends `pcm` to stream 0 or 1 and returns whatever can be mixed now
    // (possibly empty). Odd trailing bytes wait for their partner byte.
    [[nodiscard]] QByteArray push(int stream, const QByteArray &pcm);
    // Mixes and returns everything still buffered (end of session).
    [[nodiscard]] QByteArray drain();

private:
    [[nodiscard]] QByteArray take(qsizetype samples);

    QByteArray m_buffers[2];
};

} // namespace QindaOffice::Speech
