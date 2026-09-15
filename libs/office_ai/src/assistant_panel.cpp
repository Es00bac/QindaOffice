// SPDX-License-Identifier: GPL-3.0-or-later
#include "assistant_panel.h"

#include "model_catalog.h"
#include "secret_store.h"

#include <QComboBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace QindaOffice::Ai {

AssistantPanel::AssistantPanel(ModelCatalog *catalog, DocumentContextProvider *context,
                               EditApplier *applier, QWidget *parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_context(context)
    , m_applier(applier)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *top = new QHBoxLayout;
    m_modelPicker = new QComboBox(this);
    for (const QString &id : catalog->modelIds()) {
        const auto spec = catalog->specFor(id);
        QString label = spec.has_value() ? spec->displayName : id;
        if (spec.has_value() && spec->offMachine) {
            label += tr(" (leaves this machine)");
        }
        m_modelPicker->addItem(label, id);
    }
    top->addWidget(m_modelPicker, 1);
    m_contextScope = new QComboBox(this);
    m_contextScope->addItem(tr("No context"));
    m_contextScope->addItem(tr("Selection"));
    m_contextScope->addItem(tr("Whole document"));
    top->addWidget(m_contextScope);
    layout->addLayout(top);

    m_conversation = new QTextBrowser(this);
    layout->addWidget(m_conversation, 1);

    auto *replyActions = new QHBoxLayout;
    m_insert = new QToolButton(this);
    m_insert->setText(tr("Insert"));
    m_replace = new QToolButton(this);
    m_replace->setText(tr("Replace selection"));
    m_copy = new QToolButton(this);
    m_copy->setText(tr("Copy"));
    for (QToolButton *button : {m_insert, m_replace, m_copy}) {
        button->setEnabled(false);
        replyActions->addWidget(button);
    }
    layout->addLayout(replyActions);

    auto *bottom = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Ask the assistant…"));
    m_send = new QToolButton(this);
    m_send->setText(tr("Send"));
    m_stop = new QToolButton(this);
    m_stop->setText(tr("Stop"));
    m_stop->setEnabled(false);
    bottom->addWidget(m_input, 1);
    bottom->addWidget(m_send);
    bottom->addWidget(m_stop);
    layout->addLayout(bottom);

    connect(m_input, &QLineEdit::returnPressed, m_send, &QToolButton::click);
    connect(m_send, &QToolButton::clicked, this, [this] {
        const QString prompt = m_input->text().trimmed();
        if (!prompt.isEmpty()) {
            sendPrompt(prompt);
        }
    });
    connect(m_stop, &QToolButton::clicked, this, &AssistantPanel::stop);
    connect(m_copy, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_lastReply);
    });
    connect(m_insert, &QToolButton::clicked, this, [this] {
        if (m_applier != nullptr && !m_lastReply.isEmpty()) {
            m_applier->insertAtCursor(m_lastReply);
        }
    });
    connect(m_replace, &QToolButton::clicked, this, [this] {
        if (m_applier != nullptr && !m_lastReply.isEmpty()) {
            m_applier->replaceSelection(m_lastReply);
        }
    });
}

QString AssistantPanel::currentModelId() const
{
    return m_modelPicker->currentData().toString();
}

void AssistantPanel::appendMessage(const QString &role, const QString &text)
{
    const QString colour = role == QLatin1String("user") ? QStringLiteral("#884400")
                                                         : QStringLiteral("#004488");
    m_conversation->append(QStringLiteral("<b style='color:%1'>%2</b><br>%3")
                               .arg(colour, role, text.toHtmlEscaped().replace(QLatin1Char('\n'),
                                                                               QStringLiteral("<br>"))));
}

void AssistantPanel::sendPrompt(const QString &prompt)
{
    if (m_run != nullptr) {
        return; // one conversation turn at a time
    }
    appendMessage(QStringLiteral("you"), prompt);
    m_input->clear();

    ChatRequest request;
    request.messages.append({QStringLiteral("user"), prompt, {}});
    const int scope = m_contextScope->currentIndex();
    if (m_context != nullptr && scope > 0) {
        const DocumentContext context = m_context->context(scope == 2);
        QString material;
        if (scope == 1 && !context.selection.isEmpty()) {
            material = tr("Selected text:\n%1").arg(context.selection);
        } else if (scope == 2) {
            material = tr("Document:\n%1").arg(context.document);
        }
        if (!material.isEmpty()) {
            request.messages.prepend({QStringLiteral("user"), material, {}});
        }
        if (!context.region.isNull()) {
            request.messages.last().images.append(context.region);
        }
    }
    runRequest(request);
}

void AssistantPanel::runRequest(const ChatRequest &request)
{
    const QString modelId = currentModelId();
    // Resolve the secret (if any) then start the run.
    auto startWith = [this, modelId, request](const QString &secret) {
        ChatProvider *provider = m_catalog->createProvider(modelId, secret, this);
        if (provider == nullptr) {
            appendMessage(QStringLiteral("error"), tr("provider unavailable"));
            return;
        }
        m_run = provider->start(request);
        m_stop->setEnabled(true);
        connect(m_run, &ChatRun::textDelta, this, [this](const QString &delta) {
            m_lastReply += delta;
            m_conversation->moveCursor(QTextCursor::End);
            m_conversation->insertPlainText(delta);
        });
        connect(m_run, &ChatRun::finished, this, [this](const ChatResult &result) {
            m_run = nullptr;
            m_stop->setEnabled(false);
            m_lastReply = result.text;
            m_insert->setEnabled(!m_lastReply.isEmpty());
            m_replace->setEnabled(!m_lastReply.isEmpty());
            m_copy->setEnabled(!m_lastReply.isEmpty());
        });
        connect(m_run, &ChatRun::failed, this, [this](const ChatError &error) {
            m_run = nullptr;
            m_stop->setEnabled(false);
            appendMessage(QStringLiteral("error"), error.message);
        });
    };
    ProviderConfig config;
    for (const ProviderConfig &candidate : m_catalog->providers()) {
        if (modelId == candidate.id || modelId == candidate.id + QStringLiteral("/") + candidate.model) {
            config = candidate;
        }
    }
    if (config.secretKey.isEmpty()) {
        startWith(QString());
        return;
    }
    // Resolve through the keychain-backed store (async but near-instant on
    // the session bus; a empty secret surfaces as an auth error later).
    auto *store = new SecretStore(this);
    store->retrieve(config.secretKey, [this, store, startWith](const QString &value) {
        startWith(value);
        store->deleteLater();
    });
}

void AssistantPanel::stop()
{
    if (m_run != nullptr) {
        m_run->cancel();
    }
}

} // namespace QindaOffice::Ai
