// SPDX-License-Identifier: GPL-3.0-or-later
// office_speech gates (D-057, docs/live-notes.md).

#include "elevenlabs_realtime.h"
#include "http_transcription.h"
#include "pcm_capture.h"
#include "pcm_mixer.h"
#include "speech_segmenter.h"
#include "whisper_local.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtEndian>
#include <QtTest>

#include <cmath>

using namespace QindaOffice::Speech;

namespace {

// `ms` of a 440 Hz tone at `amplitude` (0..1), or silence at 0.
QByteArray tone(int ms, double amplitude)
{
    const int samples = ms * kSampleRate / 1000;
    QByteArray out(samples * 2, Qt::Uninitialized);
    for (int i = 0; i < samples; ++i) {
        const double v = amplitude * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
        qToLittleEndian<qint16>(qint16(v * 32767), out.data() + i * 2);
    }
    return out;
}

qint16 sampleAt(const QByteArray &pcm, int index)
{
    return qFromLittleEndian<qint16>(pcm.constData() + index * 2);
}

QString writeScript(const QTemporaryDir &dir, const QString &name, const QByteArray &body)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(body);
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return path;
}

} // namespace

class OfficeSpeechTest : public QObject {
    Q_OBJECT
private slots:
    void mixerSumsAndSaturates();
    void mixerTreatsALaggingStreamAsSilence();
    void segmenterCutsUtterances();
    void segmenterDropsBlipsAndSplitsMonologues();
    void wavHeaderDescribesThePcm();
    void captureReadsTheRecorderInChunks();
    void captureReportsAMissingRecorder();
    void elevenLabsStreamsAndCommits();
    void elevenLabsAuthErrorIsFatal();
    void elevenLabsWithoutKeyFinishesAtOnce();
    void whisperHelperProtocol();
    void whisperRealHelperRefusesAMissingModel();
    void httpTranscriptionPostsClips();
};

void OfficeSpeechTest::mixerSumsAndSaturates()
{
    PcmMixer mixer;
    QByteArray a(4, '\0');
    QByteArray b(4, '\0');
    qToLittleEndian<qint16>(1000, a.data());
    qToLittleEndian<qint16>(30000, a.data() + 2);
    qToLittleEndian<qint16>(2000, b.data());
    qToLittleEndian<qint16>(30000, b.data() + 2);
    QVERIFY(mixer.push(0, a).isEmpty()); // waits for its partner
    const QByteArray mixed = mixer.push(1, b);
    QCOMPARE(mixed.size(), 4);
    QCOMPARE(sampleAt(mixed, 0), qint16(3000));
    QCOMPARE(sampleAt(mixed, 1), qint16(32767)); // saturated, not wrapped
}

void OfficeSpeechTest::mixerTreatsALaggingStreamAsSilence()
{
    PcmMixer mixer;
    // 500 ms on stream 0 and nothing on stream 1: everything beyond the
    // 300 ms lag limit comes out alone.
    const QByteArray out = mixer.push(0, tone(500, 0.5));
    QCOMPARE(out.size(), 200 * kBytesPerMs);
    QCOMPARE(mixer.drain().size(), 300 * kBytesPerMs);
}

void OfficeSpeechTest::segmenterCutsUtterances()
{
    SpeechSegmenter segmenter;
    QList<AudioClip> clips;
    clips += segmenter.feed(tone(1000, 0.0));
    clips += segmenter.feed(tone(1500, 0.3));
    QVERIFY(segmenter.inSpeech());
    clips += segmenter.feed(tone(1000, 0.0));
    QCOMPARE(clips.size(), 1);
    const AudioClip clip = clips.first();
    // Onset at 1000 ms, minus the pre-roll; ends after 700 ms of silence.
    QVERIFY2(qAbs(clip.startMs - (1000 - SpeechSegmenter::kPreRollMs)) <= SpeechSegmenter::kFrameMs,
             qPrintable(QString::number(clip.startMs)));
    QVERIFY(clip.endMs >= 2500 + SpeechSegmenter::kSilenceMs - SpeechSegmenter::kFrameMs);
    QCOMPARE(clip.pcm.size(), (clip.endMs - clip.startMs) * kBytesPerMs);
}

void OfficeSpeechTest::segmenterDropsBlipsAndSplitsMonologues()
{
    SpeechSegmenter blips;
    QList<AudioClip> clips = blips.feed(tone(500, 0.0) + tone(120, 0.5) + tone(1500, 0.0));
    QVERIFY2(clips.isEmpty(), "a 120 ms click is not speech");

    SpeechSegmenter monologue;
    clips = monologue.feed(tone(45000, 0.3));
    clips += monologue.flush();
    QCOMPARE(clips.size(), 3); // 20 s + 20 s + the rest
    // Cut on the first frame boundary at or past the limit.
    QVERIFY(clips.at(0).endMs - clips.at(0).startMs < SpeechSegmenter::kMaxMs + SpeechSegmenter::kFrameMs);
}

void OfficeSpeechTest::wavHeaderDescribesThePcm()
{
    const QByteArray pcm = tone(100, 0.1);
    const QByteArray wav = HttpTranscription::wavFile(pcm);
    QCOMPARE(wav.size(), 44 + pcm.size());
    QCOMPARE(wav.left(4), QByteArray("RIFF"));
    QCOMPARE(wav.mid(8, 8), QByteArray("WAVEfmt "));
    QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 24), quint32(kSampleRate));
    QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 40), quint32(pcm.size()));
}

void OfficeSpeechTest::captureReadsTheRecorderInChunks()
{
    QTemporaryDir dir;
    // A pw-record stand-in: records its arguments, writes 1 s of PCM, waits.
    const QString args = dir.filePath("args.txt");
    const QString script = writeScript(dir, "pw-record",
        "#!/bin/sh\necho \"$@\" >> " + args.toUtf8() + "\n"
        "head -c 32000 /dev/urandom\nsleep 30\n");
    qputenv("QQO_PW_RECORD", script.toUtf8());
    qputenv("QQO_DEFAULT_SINK", "test.sink");
    PcmCapture capture;
    QSignalSpy chunks(&capture, &PcmCapture::pcm);
    QSignalSpy levels(&capture, &PcmCapture::level);
    QVERIFY(capture.start(PcmCapture::Source::DesktopAudio));
    QTRY_VERIFY(chunks.size() >= 10);
    for (const QList<QVariant> &chunk : chunks) {
        QCOMPARE(chunk.first().toByteArray().size(), 100 * kBytesPerMs);
    }
    QVERIFY(!levels.isEmpty());
    capture.stop();
    QFile recorded(args);
    QVERIFY(recorded.open(QIODevice::ReadOnly));
    const QByteArray line = recorded.readAll();
    QVERIFY2(line.contains("stream.capture.sink = true") && line.contains("--target test.sink")
                 && line.contains("--rate 16000") && line.contains("--channels 1"),
             line.constData());

    // Paused: the level keeps moving, the audio stops.
    QVERIFY(capture.start(PcmCapture::Source::Microphone));
    capture.setPaused(true);
    chunks.clear();
    levels.clear();
    QTRY_VERIFY(levels.size() >= 5);
    QCOMPARE(chunks.size(), 0);
    capture.stop();
    qunsetenv("QQO_PW_RECORD");
    qunsetenv("QQO_DEFAULT_SINK");
}

void OfficeSpeechTest::captureReportsAMissingRecorder()
{
    qputenv("QQO_PW_RECORD", "/nonexistent/pw-record");
    PcmCapture capture;
    QSignalSpy failed(&capture, &PcmCapture::failed);
    QVERIFY(!capture.start(PcmCapture::Source::Microphone));
    QCOMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("pw-record")));
    qunsetenv("QQO_PW_RECORD");
}

void OfficeSpeechTest::elevenLabsStreamsAndCommits()
{
    QWebSocketServer server(QStringLiteral("mock-elevenlabs"), QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray seenKey;
    QUrl seenUrl;
    int audioMessages = 0;
    bool sawPreviousText = false;
    connect(&server, &QWebSocketServer::newConnection, this, [&] {
        QWebSocket *socket = server.nextPendingConnection();
        seenKey = socket->request().rawHeader("xi-api-key");
        seenUrl = socket->requestUrl();
        connect(socket, &QWebSocket::textMessageReceived, socket, [&, socket](const QString &text) {
            const QJsonObject m = QJsonDocument::fromJson(text.toUtf8()).object();
            if (m.value(QStringLiteral("message_type")).toString() != QLatin1String("input_audio_chunk")) {
                return;
            }
            sawPreviousText = sawPreviousText || m.contains(QStringLiteral("previous_text"));
            if (!m.value(QStringLiteral("audio_base_64")).toString().isEmpty()) {
                ++audioMessages;
            }
            if (audioMessages == 2) {
                socket->sendTextMessage(QStringLiteral(
                    R"({"message_type":"partial_transcript","text":"photosynthesis converts"})"));
                socket->sendTextMessage(QStringLiteral(
                    R"({"message_type":"committed_transcript","text":"Photosynthesis converts light."})"));
            }
            if (m.value(QStringLiteral("commit")).toBool()) {
                socket->sendTextMessage(QStringLiteral(
                    R"({"message_type":"committed_transcript","text":"The end."})"));
            }
        });
        socket->sendTextMessage(QStringLiteral(R"({"message_type":"session_started","session_id":"s1"})"));
    });

    ElevenLabsRealtime recognizer(QStringLiteral("test-key"), QStringLiteral("en"));
    recognizer.setEndpoint(QUrl(QStringLiteral("ws://127.0.0.1:%1/v1/speech-to-text/realtime")
                                    .arg(server.serverPort())));
    recognizer.setKeyterms({QStringLiteral("QindaNote")});
    QSignalSpy partials(&recognizer, &SpeechRecognizer::partial);
    QSignalSpy committed(&recognizer, &SpeechRecognizer::committed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    // Audio before the session opens is buffered, then flushed.
    recognizer.start();
    recognizer.feed(tone(300, 0.2));
    QTRY_VERIFY(!seenUrl.isEmpty());
    recognizer.feed(tone(300, 0.2));
    QTRY_COMPARE(committed.size(), 1);
    QCOMPARE(seenKey, QByteArray("test-key"));
    const QString query = seenUrl.query();
    QVERIFY2(query.contains(QStringLiteral("model_id=scribe_v2_realtime"))
                 && query.contains(QStringLiteral("audio_format=pcm_16000"))
                 && query.contains(QStringLiteral("commit_strategy=vad"))
                 && query.contains(QStringLiteral("language_code=en"))
                 && query.contains(QStringLiteral("keyterms=QindaNote")),
             qPrintable(query));
    QVERIFY(!partials.isEmpty());
    QCOMPARE(committed.first().first().value<SpeechSegment>().text,
             QStringLiteral("Photosynthesis converts light."));
    QVERIFY(!sawPreviousText); // nothing to carry on the first session
    recognizer.finish();
    QTRY_COMPARE(committed.size(), 2);
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(committed.last().first().value<SpeechSegment>().text, QStringLiteral("The end."));
}

void OfficeSpeechTest::elevenLabsAuthErrorIsFatal()
{
    QWebSocketServer server(QStringLiteral("mock-elevenlabs"), QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QWebSocketServer::newConnection, this, [&] {
        server.nextPendingConnection()->sendTextMessage(
            QStringLiteral(R"({"message_type":"auth_error","error":"invalid api key"})"));
    });
    ElevenLabsRealtime recognizer(QStringLiteral("bad"), QString());
    recognizer.setEndpoint(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(server.serverPort())));
    QSignalSpy failed(&recognizer, &SpeechRecognizer::failed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    recognizer.start();
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed.first().at(1).toBool());
    QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("invalid api key")));
    QTRY_COMPARE(finished.size(), 1);
}

void OfficeSpeechTest::elevenLabsWithoutKeyFinishesAtOnce()
{
    ElevenLabsRealtime recognizer{QString(), QString()};
    QSignalSpy failed(&recognizer, &SpeechRecognizer::failed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    // A receiver that calls finish() from failed() (the session does).
    connect(&recognizer, &SpeechRecognizer::failed, &recognizer, &SpeechRecognizer::finish);
    recognizer.start();
    QCOMPARE(failed.size(), 1);
    QVERIFY(failed.first().at(1).toBool());
    QCOMPARE(finished.size(), 1);
}

void OfficeSpeechTest::whisperHelperProtocol()
{
    QTemporaryDir dir;
    // Answers every frame with the clip's byte count, after a "ready".
    const QString helper = writeScript(dir, "fake-whisper", R"PY(#!/usr/bin/env python3
import json, sys
print(json.dumps({"ready": True, "model": sys.argv[sys.argv.index("--model") + 1]}), flush=True)
stdin = sys.stdin.buffer
while True:
    header = stdin.readline()
    if not header:
        break
    frame = json.loads(header)
    pcm = stdin.read(frame["bytes"])
    with open(sys.argv[0] + ".prompts", "a") as log:
        log.write(frame.get("prompt", "") + "\n")
    print(json.dumps({"id": frame["id"], "text": "clip of %d bytes" % len(pcm)}), flush=True)
)PY");
    qputenv("QQO_WHISPER_HELPER", helper.toUtf8());
    WhisperLocal recognizer(QStringLiteral("distil-small.en"), QStringLiteral("en"));
    recognizer.setKeyterms({QStringLiteral("Jev"), QStringLiteral("Laya")});
    QCOMPARE(WhisperLocal::helperPath(), helper);
    QVERIFY(!recognizer.offMachine());
    QSignalSpy ready(&recognizer, &SpeechRecognizer::ready);
    QSignalSpy committed(&recognizer, &SpeechRecognizer::committed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    recognizer.start();
    recognizer.feed(tone(500, 0.0) + tone(1000, 0.3) + tone(1000, 0.0));
    recognizer.feed(tone(800, 0.3)); // still open when finish() flushes it
    recognizer.finish();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(committed.size(), 2);
    const SpeechSegment first = committed.at(0).first().value<SpeechSegment>();
    QVERIFY(first.text.startsWith(QStringLiteral("clip of ")));
    QVERIFY(first.startMs < first.endMs);
    // The helper echoes the prompt it got: the glossary rides on every clip.
    QFile prompts(helper + QStringLiteral(".prompts"));
    QVERIFY(prompts.open(QIODevice::ReadOnly));
    const QByteArray log = prompts.readAll();
    QCOMPARE(log.count("Glossary: Jev, Laya."), 2);
    qunsetenv("QQO_WHISPER_HELPER");
}

void OfficeSpeechTest::whisperRealHelperRefusesAMissingModel()
{
    qputenv("QQO_WHISPER_HELPER", QQO_WHISPER_HELPER_REAL);
    WhisperLocal recognizer(QStringLiteral("no-such-model-anywhere"), QString());
    QSignalSpy failed(&recognizer, &SpeechRecognizer::failed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    recognizer.start();
    QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 20000);
    const QString message = failed.first().first().toString();
    // Either faster-whisper is missing on this machine, or the model is:
    // both are fatal and both name the Portage package to install.
    QVERIFY2(message.contains(QStringLiteral("faster-whisper")), qPrintable(message));
    QVERIFY(failed.first().at(1).toBool());
    // The helper imports faster-whisper before it can refuse: slow on a
    // busy machine.
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
    qunsetenv("QQO_WHISPER_HELPER");
}

void OfficeSpeechTest::httpTranscriptionPostsClips()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray request;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            request += socket->readAll();
            const qsizetype headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const qsizetype lengthAt = request.toLower().indexOf("content-length:");
            const qsizetype length = request.mid(lengthAt + 15, request.indexOf("\r\n", lengthAt) - lengthAt - 15)
                                         .trimmed().toLongLong();
            if (request.size() - headerEnd - 4 < length) {
                return;
            }
            const QByteArray body = R"({"text":"hello from the endpoint"})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });
    HttpTranscription recognizer(QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                                 QStringLiteral("k"), QStringLiteral("whisper-large-v3-turbo"),
                                 QStringLiteral("en"));
    QVERIFY(!recognizer.offMachine()); // loopback
    QSignalSpy committed(&recognizer, &SpeechRecognizer::committed);
    QSignalSpy finished(&recognizer, &SpeechRecognizer::finished);
    recognizer.start();
    recognizer.feed(tone(400, 0.0) + tone(1200, 0.3) + tone(1000, 0.0));
    QTRY_COMPARE(committed.size(), 1);
    QCOMPARE(committed.first().first().value<SpeechSegment>().text,
             QStringLiteral("hello from the endpoint"));
    QVERIFY(request.startsWith("POST /v1/audio/transcriptions"));
    QVERIFY(request.contains("Authorization: Bearer k"));
    QVERIFY(request.contains("name=\"model\"\r\n\r\nwhisper-large-v3-turbo"));
    QVERIFY(request.contains("RIFF"));
    recognizer.finish();
    QTRY_COMPARE(finished.size(), 1);
}

QTEST_GUILESS_MAIN(OfficeSpeechTest)

#include "tst_office_speech.moc"
