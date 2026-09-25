// SPDX-License-Identifier: GPL-3.0-or-later
#include "provider_calls.h"

#include <QMetaMethod>
#include <QUuid>

namespace QindaOffice::Apps::Mail {

ProviderCalls::ProviderCalls(QObject *parent)
    : QObject(parent)
{
}

void ProviderCalls::watch(QObject *provider)
{
    if (provider == nullptr || provider->property("_qqoMailWatched").toBool()) {
        return;
    }
    provider->setProperty("_qqoMailWatched", true);
    const QMetaObject *meta = provider->metaObject();
    const int slot = metaObject()->indexOfSlot("deliver(QString,QVariantMap)");
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Signal || method.parameterCount() != 2
            || method.parameterMetaType(0) != QMetaType::fromType<QString>()
            || method.parameterMetaType(1) != QMetaType::fromType<QVariantMap>()) {
            continue;
        }
        const QByteArray name = method.name();
        if (name == "finished" || name.endsWith("Finished")) {
            QMetaObject::connect(provider, i, this, slot);
        }
    }
}

void ProviderCalls::expect(const QString &requestId, Callback callback)
{
    m_callbacks.insert(requestId, std::move(callback));
}

void ProviderCalls::deliver(const QString &requestId, const QVariantMap &result)
{
    const auto it = m_callbacks.find(requestId);
    if (it == m_callbacks.end()) {
        return;
    }
    // Take it out first: the callback may start the next request (a retry)
    // under a new id, or destroy the provider that is emitting.
    Callback callback = std::move(it.value());
    m_callbacks.erase(it);
    callback(result);
}

QString ProviderCalls::newRequestId(const QString &prefix)
{
    static quint64 serial = 0;
    return QStringLiteral("%1-%2-%3")
        .arg(prefix)
        .arg(++serial)
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
}

QVariantMap ProviderCalls::result(const QString &requestId, const QString &state, const QString &error,
                                  bool retryable)
{
    return {
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("ok"), state == QLatin1String("accepted")},
        {QStringLiteral("state"), state},
        {QStringLiteral("error"), error},
        {QStringLiteral("retryable"), retryable},
    };
}

} // namespace QindaOffice::Apps::Mail
