// SPDX-License-Identifier: GPL-3.0-or-later
#include "api_key_resolver.h"

#include "secret_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

namespace QindaOffice::Ai {

namespace {

QString synchronousSteps(const KeySources &sources, QString *origin)
{
    if (!sources.reference.isEmpty()) {
        const QString key = ApiKeyResolver::fromReference(sources.reference);
        if (!key.isEmpty()) {
            *origin = sources.reference.startsWith(QLatin1String("{env:"))
                ? QObject::tr("environment") : QObject::tr("file");
            return key;
        }
    }
    QString which;
    QString key = ApiKeyResolver::fromEnvironment(sources.envVars, &which);
    if (!key.isEmpty()) {
        *origin = QObject::tr("$%1").arg(which);
        return key;
    }
    key = ApiKeyResolver::fromOpenCode(sources.openCodeId);
    if (!key.isEmpty()) {
        *origin = QObject::tr("OpenCode");
        return key;
    }
    key = ApiKeyResolver::fromSecretService(sources.secretAttributes);
    if (!key.isEmpty()) {
        *origin = QObject::tr("Secret Service");
        return key;
    }
    return {};
}

} // namespace

bool ApiKeyResolver::isReference(const QString &text)
{
    const QString t = text.trimmed();
    return (t.startsWith(QLatin1String("{env:")) || t.startsWith(QLatin1String("{file:")))
        && t.endsWith(QLatin1Char('}'));
}

QString ApiKeyResolver::expandHome(const QString &path)
{
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

QString ApiKeyResolver::fromReference(const QString &reference)
{
    const QString t = reference.trimmed();
    if (!isReference(t)) {
        return {};
    }
    const QString body = t.mid(t.indexOf(QLatin1Char(':')) + 1).chopped(1).trimmed();
    if (t.startsWith(QLatin1String("{env:"))) {
        return qEnvironmentVariable(body.toLocal8Bit().constData()).trimmed();
    }
    QFile file(expandHome(body));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    // A key file is one line; never read a stray large file into a header.
    return QString::fromUtf8(file.read(4096)).trimmed();
}

QString ApiKeyResolver::fromEnvironment(const QStringList &names, QString *which)
{
    for (const QString &name : names) {
        const QString value = qEnvironmentVariable(name.toLocal8Bit().constData()).trimmed();
        if (!value.isEmpty()) {
            if (which != nullptr) {
                *which = name;
            }
            return value;
        }
    }
    return {};
}

QString ApiKeyResolver::fromOpenCode(const QString &providerId)
{
    if (providerId.isEmpty()) {
        return {};
    }
    // OpenCode keeps auth.json under XDG_DATA_HOME/opencode.
    const QString dataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME")
        ? qEnvironmentVariable("XDG_DATA_HOME")
        : QDir::homePath() + QStringLiteral("/.local/share");
    QFile file(dataHome + QStringLiteral("/opencode/auth.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject entry =
        QJsonDocument::fromJson(file.readAll()).object().value(providerId).toObject();
    if (entry.value(QStringLiteral("type")).toString() != QLatin1String("api")) {
        return {}; // oauth entries are OpenCode's own sessions, not API keys
    }
    return entry.value(QStringLiteral("key")).toString().trimmed();
}

QString ApiKeyResolver::fromSecretService(const QStringList &attributes)
{
    if (attributes.isEmpty() || attributes.size() % 2 != 0
        || qEnvironmentVariableIsSet("QQO_NO_SECRET_SERVICE")) {
        return {};
    }
    const QString program = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (program.isEmpty()) {
        return {};
    }
    QProcess lookup;
    lookup.start(program, QStringList{QStringLiteral("lookup")} + attributes);
    if (!lookup.waitForFinished(4000) || lookup.exitCode() != 0) {
        lookup.kill();
        return {};
    }
    return QString::fromUtf8(lookup.readAllStandardOutput()).trimmed();
}

void ApiKeyResolver::resolve(const KeySources &sources, QObject *context,
                             const Callback &callback)
{
    const auto fallBack = [sources, callback] {
        QString origin;
        const QString key = synchronousSteps(sources, &origin);
        callback(key, key.isEmpty() ? QString() : origin);
    };
    if (sources.keyringKey.isEmpty() || qEnvironmentVariableIsSet("QQO_NO_SECRET_SERVICE")) {
        fallBack();
        return;
    }
    auto *store = new SecretStore(context);
    QPointer<QObject> guard(context);
    store->retrieve(sources.keyringKey, [store, guard, callback, fallBack](const QString &value) {
        store->deleteLater();
        if (guard == nullptr) {
            return;
        }
        if (!value.trimmed().isEmpty()) {
            callback(value.trimmed(), QObject::tr("keyring"));
            return;
        }
        fallBack();
    });
}

} // namespace QindaOffice::Ai
