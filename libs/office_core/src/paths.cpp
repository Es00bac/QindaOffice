// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.h"

#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>

namespace QindaOffice::Core {
namespace {

QString ensure(const QString &path)
{
    QDir().mkpath(path);
    return path;
}

} // namespace

QString configDirFor(const QString &appLeaf)
{
    // AGENT-NOTE: plain ConfigLocation (not AppConfigLocation): the plan
    // pins the suite root at $XDG_CONFIG_HOME/qindaoffice/<app>/; the
    // organization/application segments would double the nesting.
    return ensure(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                  + QStringLiteral("/qindaoffice/") + appLeaf);
}

QString stateDirFor(const QString &appLeaf)
{
    return ensure(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
                  + QStringLiteral("/qindaoffice/") + appLeaf);
}

QString appLeaf()
{
    const QString name = qGuiApp != nullptr ? QGuiApplication::desktopFileName() : QString();
    if (name.isEmpty()) {
        return QStringLiteral("Office");
    }
    return name.section(QLatin1Char('.'), -1);
}

} // namespace QindaOffice::Core
