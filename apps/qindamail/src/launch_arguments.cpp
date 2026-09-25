// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch_arguments.h"

#include <QDir>
#include <QUrl>

namespace QindaOffice::Apps::Mail::Launch {

namespace {

QString encode(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QString decode(const QString &value)
{
    return QUrl::fromPercentEncoding(value.toUtf8());
}

} // namespace

QStringList foldAttachArguments(const QStringList &arguments, const QString &workingDir)
{
    QStringList rest;
    QStringList attachments;
    const QDir base(workingDir);
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        QString path;
        if (a == QLatin1String("--attach") && i + 1 < arguments.size()) {
            path = arguments.at(++i);
        } else if (a.startsWith(QLatin1String("--attach="))) {
            path = a.mid(9);
        } else {
            rest << a;
            continue;
        }
        if (!path.isEmpty()) {
            attachments << QDir::cleanPath(base.absoluteFilePath(path));
        }
    }
    if (attachments.isEmpty()) {
        return arguments;
    }
    QString mailto;
    for (qsizetype i = 0; i < rest.size(); ++i) {
        if (rest.at(i).startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
            mailto = rest.takeAt(i);
            break;
        }
    }
    QStringList query;
    for (const QString &path : std::as_const(attachments)) {
        query << QStringLiteral("attach=") + encode(path);
    }
    if (!mailto.isEmpty()) {
        query << QStringLiteral("mailto=") + encode(mailto);
    }
    rest << QString::fromLatin1(kComposeScheme) + QLatin1Char('?') + query.join(QLatin1Char('&'));
    return rest;
}

ComposeRequest parseComposeToken(const QString &token)
{
    ComposeRequest request;
    const QString scheme = QString::fromLatin1(kComposeScheme);
    if (!token.startsWith(scheme, Qt::CaseInsensitive)) {
        return request;
    }
    QString query = token.mid(scheme.size());
    if (query.startsWith(QLatin1Char('?'))) {
        query.remove(0, 1);
    }
    for (const QString &pair : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        const QString name = eq < 0 ? pair : pair.left(eq);
        const QString value = eq < 0 ? QString() : decode(pair.mid(eq + 1));
        if (name == QLatin1String("attach") && QDir::isAbsolutePath(value)) {
            request.attachments << value;
        } else if (name == QLatin1String("mailto") && value.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
            request.mailto = value;
        }
    }
    request.ok = true;
    return request;
}

} // namespace QindaOffice::Apps::Mail::Launch
