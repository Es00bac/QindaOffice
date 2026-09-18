// SPDX-License-Identifier: GPL-3.0-or-later
#include "template_loader.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace QindaOffice::Quick {

QList<TemplateEntry> templatesIn(const QString &directory)
{
    QList<TemplateEntry> out;
    const QDir dir(directory);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        QString name = info.completeBaseName();
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        name.replace(QLatin1Char('-'), QLatin1Char(' '));
        out.append({name, info.absoluteFilePath()});
    }
    return out;
}

QList<TemplateEntry> templatesFor(const QString &appName)
{
    const QStringList roots = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("qindaoffice/templates"),
        QStandardPaths::LocateDirectory);
    QList<TemplateEntry> out;
    for (const QString &root : roots) {
        out += templatesIn(root + QLatin1Char('/') + appName);
    }
    return out;
}

} // namespace QindaOffice::Quick
