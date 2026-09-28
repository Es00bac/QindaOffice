// SPDX-License-Identifier: GPL-3.0-or-later
// LiveNotesSession's settings (note_live_session.h): persisted per app in
// live-notes.json next to the model catalog (AGENTS.md rule 7: bounded,
// per app); keys never — they live in the keyring (D-052) or come from
// the environment / Gabbee's keyring item / OpenCode (ApiKeyResolver).

#include "note_live_session.h"

#include "note_host.h"

#include "json_settings.h"
#include "model_catalog.h"
#include "secret_store.h"

#include <QUrl>

namespace QindaOffice::Apps::Notes {

namespace {

// Keyring ids (service "qindaoffice").
const QString kElevenLabsKey = QStringLiteral("elevenlabs-api-key");
const QString kTranscriptionKey = QStringLiteral("transcription-api-key");
const QString kGateKey = QStringLiteral("systemone-api-key");

// The System One servers (system_one_client.h).
const QString kJevBase = QStringLiteral("https://api.typesafe.ai");
const QString kJevModel = QStringLiteral("jev-1.13.0");
const QString kLayaBase = QStringLiteral("http://127.0.0.1:8000");
const QString kLayaModel = QStringLiteral("english");

bool loopback(const QString &url)
{
    const QString host = QUrl(url).host();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1");
}

} // namespace

LiveNotesSession::LiveNotesSession(NoteHost *host, Ai::ModelCatalog *catalog,
                                   QString settingsPath, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_catalog(catalog)
    , m_settings(std::make_unique<Core::JsonSettings>(settingsPath))
{
    loadSettings();
    if (m_catalog != nullptr) {
        connect(m_catalog, &Ai::ModelCatalog::changed, this, [this] {
            if (!m_catalog->configFor(m_modelId).has_value() && !m_catalog->modelIds().isEmpty()) {
                m_modelId = m_catalog->modelIds().first();
                emit settingsChanged();
            }
            emit modelsChanged();
        });
    }
}

LiveNotesSession::~LiveNotesSession()
{
    // Leaving mid-session (the window closes): keep what was written and
    // the transcript, stop the processes.
    if (active()) {
        cancel();
    }
}

void LiveNotesSession::loadSettings()
{
    const auto text = [this](const char *key, const QString &fallback) {
        return m_settings->value(QLatin1String(key), fallback).toString();
    };
    m_source = text("source", m_source);
    m_engine = text("engine", m_engine);
    m_language = text("language", m_language);
    m_whisperModel = text("whisperModel", m_whisperModel);
    m_httpBaseUrl = text("httpBaseUrl", QStringLiteral("https://api.groq.com/openai/v1"));
    m_httpModel = text("httpModel", QStringLiteral("whisper-large-v3-turbo"));
    m_keyterms = text("keyterms", m_keyterms);
    m_modelId = text("modelId", QString());
    m_gate = text("gate", m_gate);
    m_gateBaseUrl = text("gateBaseUrl", m_gate == QLatin1String("laya") ? kLayaBase : kJevBase);
    m_gateModel = text("gateModel", m_gate == QLatin1String("laya") ? kLayaModel : kJevModel);
    m_topic = text("topic", QString());
    m_briefing = Ai::NotesBriefing::fromMap(m_settings->value(QStringLiteral("setup")).toMap());
    m_askBeforeStart = m_settings->value(QStringLiteral("askBeforeStart"), true).toBool();
    m_detail = Ai::LiveNotesPrompt::clampDetail(
        m_settings->value(QStringLiteral("detail"), m_detail).toInt());
    if (m_catalog != nullptr && !m_catalog->configFor(m_modelId).has_value()) {
        const QStringList ids = m_catalog->modelIds();
        m_modelId = ids.isEmpty() ? QString() : ids.first();
    }
}

void LiveNotesSession::saveSettings()
{
    m_settings->setValue(QStringLiteral("source"), m_source);
    m_settings->setValue(QStringLiteral("engine"), m_engine);
    m_settings->setValue(QStringLiteral("language"), m_language);
    m_settings->setValue(QStringLiteral("whisperModel"), m_whisperModel);
    m_settings->setValue(QStringLiteral("httpBaseUrl"), m_httpBaseUrl);
    m_settings->setValue(QStringLiteral("httpModel"), m_httpModel);
    m_settings->setValue(QStringLiteral("keyterms"), m_keyterms);
    m_settings->setValue(QStringLiteral("modelId"), m_modelId);
    m_settings->setValue(QStringLiteral("gate"), m_gate);
    m_settings->setValue(QStringLiteral("gateBaseUrl"), m_gateBaseUrl);
    m_settings->setValue(QStringLiteral("gateModel"), m_gateModel);
    m_settings->setValue(QStringLiteral("topic"), m_topic);
    m_settings->setValue(QStringLiteral("detail"), m_detail);
    m_settings->setValue(QStringLiteral("setup"), m_briefing.toMap());
    m_settings->setValue(QStringLiteral("askBeforeStart"), m_askBeforeStart);
    static_cast<void>(m_settings->save(nullptr));
    emit settingsChanged();
}

// Settings change between sessions only: the panel disables its pickers
// while one runs, and a running pipeline keeps what it started with.
#define QQO_LIVE_SETTER(Name, member)                                                             \
    void LiveNotesSession::Name(const QString &value)                                            \
    {                                                                                             \
        if (member == value.trimmed()) {                                                          \
            return;                                                                               \
        }                                                                                         \
        member = value.trimmed();                                                                 \
        saveSettings();                                                                           \
    }
QQO_LIVE_SETTER(setSource, m_source)
QQO_LIVE_SETTER(setEngine, m_engine)
QQO_LIVE_SETTER(setLanguage, m_language)
QQO_LIVE_SETTER(setWhisperModel, m_whisperModel)
QQO_LIVE_SETTER(setHttpBaseUrl, m_httpBaseUrl)
QQO_LIVE_SETTER(setHttpModel, m_httpModel)
QQO_LIVE_SETTER(setKeyterms, m_keyterms)
QQO_LIVE_SETTER(setModelId, m_modelId)
QQO_LIVE_SETTER(setGateBaseUrl, m_gateBaseUrl)
QQO_LIVE_SETTER(setGateModel, m_gateModel)
QQO_LIVE_SETTER(setTopic, m_topic)
#undef QQO_LIVE_SETTER

void LiveNotesSession::setDetail(int value)
{
    value = Ai::LiveNotesPrompt::clampDetail(value);
    if (m_detail == value) {
        return;
    }
    m_detail = value;
    saveSettings();
    // Unlike the other settings, detail applies to a running session.
    if (m_engineCore != nullptr) {
        m_engineCore->setDetail(value);
    }
}

void LiveNotesSession::setSetup(const QVariantMap &map)
{
    const Ai::NotesBriefing briefing = Ai::NotesBriefing::fromMap(map);
    if (briefing == m_briefing) {
        return;
    }
    m_briefing = briefing;
    saveSettings();
    if (m_engineCore != nullptr) {
        m_engineCore->setBriefing(briefing); // from the next update on
    }
}

QVariantList LiveNotesSession::scenarios() const
{
    QVariantList list;
    for (const auto &scenario : Ai::LiveNotesBriefing::scenarios()) {
        list.append(QVariantMap{{QStringLiteral("id"), scenario.id},
                                {QStringLiteral("name"), scenario.name},
                                {QStringLiteral("hint"), scenario.hint}});
    }
    return list;
}

void LiveNotesSession::setAskBeforeStart(bool on)
{
    if (m_askBeforeStart == on) {
        return;
    }
    m_askBeforeStart = on;
    saveSettings();
}

void LiveNotesSession::setGate(const QString &value)
{
    if (m_gate == value) {
        return;
    }
    m_gate = value;
    // Switching between the two servers fills in that server's defaults.
    if (value == QLatin1String("jev")) {
        m_gateBaseUrl = kJevBase;
        m_gateModel = kJevModel;
    } else if (value == QLatin1String("laya")) {
        m_gateBaseUrl = kLayaBase;
        m_gateModel = kLayaModel;
    }
    saveSettings();
    checkKeys();
}

QVariantList LiveNotesSession::models() const
{
    QVariantList list;
    if (m_catalog == nullptr) {
        return list;
    }
    for (const QString &id : m_catalog->modelIds()) {
        const auto config = m_catalog->configFor(id);
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), config->displayName.isEmpty() ? id : config->displayName},
            {QStringLiteral("offMachine"), Ai::ModelCatalog::isOffMachine(*config)},
        });
    }
    return list;
}

bool LiveNotesSession::audioOffMachine() const
{
    if (m_engine == QLatin1String("whisper")) {
        return false;
    }
    if (m_engine == QLatin1String("http")) {
        return !loopback(m_httpBaseUrl);
    }
    return true; // ElevenLabs
}

bool LiveNotesSession::modelOffMachine() const
{
    const auto config = m_catalog != nullptr ? m_catalog->configFor(m_modelId) : std::nullopt;
    return config.has_value() && Ai::ModelCatalog::isOffMachine(*config);
}

bool LiveNotesSession::gateOffMachine() const
{
    return m_gate != QLatin1String("off") && !loopback(m_gateBaseUrl);
}

Ai::KeySources LiveNotesSession::speechKeySources() const
{
    Ai::KeySources sources;
    if (m_engine == QLatin1String("elevenlabs")) {
        sources.keyringKey = kElevenLabsKey;
        sources.envVars = {QStringLiteral("ELEVENLABS_API_KEY")};
        // AGENT-NOTE: Gabbee (the desktop's dictation provider) keeps the
        // user's ElevenLabs key in the Secret Service under these
        // attributes (gabbee/src/gabbee/secret_store.py); reusing it means
        // dictation and live notes share one key.
        sources.secretAttributes = {QStringLiteral("application"), QStringLiteral("gabbee"),
                                    QStringLiteral("provider"), QStringLiteral("elevenlabs"),
                                    QStringLiteral("kind"), QStringLiteral("api-key")};
    } else if (m_engine == QLatin1String("http")) {
        sources.keyringKey = kTranscriptionKey;
        const QString host = QUrl(m_httpBaseUrl).host();
        if (host.contains(QLatin1String("groq"))) {
            sources.envVars = {QStringLiteral("GROQ_API_KEY")};
            sources.openCodeId = QStringLiteral("groq");
        } else if (host.contains(QLatin1String("openai"))) {
            sources.envVars = {QStringLiteral("OPENAI_API_KEY")};
            sources.openCodeId = QStringLiteral("openai");
        }
    }
    return sources; // whisper: no key
}

Ai::KeySources LiveNotesSession::gateKeySources() const
{
    Ai::KeySources sources;
    if (m_gate == QLatin1String("off")) {
        return sources;
    }
    sources.keyringKey = kGateKey;
    sources.envVars = m_gate == QLatin1String("jev")
        ? QStringList{QStringLiteral("JEV_API_KEY"), QStringLiteral("TYPESAFE_API_KEY")}
        : QStringList{QStringLiteral("LAYA_API_KEY")};
    return sources;
}

void LiveNotesSession::storeKey(const QString &which, const QString &key)
{
    const QString id = which == QLatin1String("elevenlabs")      ? kElevenLabsKey
        : which == QLatin1String("transcription")                 ? kTranscriptionKey
        : which == QLatin1String("gate")                          ? kGateKey
                                                                  : QString();
    if (id.isEmpty() || key.trimmed().isEmpty()) {
        return;
    }
    auto *store = new Ai::SecretStore(this);
    QPointer<LiveNotesSession> self(this);
    store->store(id, key.trimmed(), [self, store](bool ok) {
        store->deleteLater();
        if (self == nullptr) {
            return;
        }
        if (!ok) {
            self->setStatus(self->m_status, tr("The keyring refused to store the key."));
        }
        self->checkKeys();
    });
}

void LiveNotesSession::checkKeys()
{
    QPointer<LiveNotesSession> self(this);
    const auto record = [self](const QString &name) {
        return [self, name](const QString &key, const QString &origin) {
            if (self == nullptr) {
                return;
            }
            self->m_keyOrigins.insert(name, key.isEmpty() ? QString() : origin);
            emit self->keyOriginsChanged();
        };
    };
    Ai::ApiKeyResolver::resolve(speechKeySources(), this, record(QStringLiteral("speech")));
    Ai::ApiKeyResolver::resolve(gateKeySources(), this, record(QStringLiteral("gate")));
    const auto config = m_catalog != nullptr ? m_catalog->configFor(m_modelId) : std::nullopt;
    if (config.has_value()) {
        Ai::ApiKeyResolver::resolve(Ai::ModelCatalog::keySources(*config), this,
                                    record(QStringLiteral("model")));
    }
}

} // namespace QindaOffice::Apps::Notes
