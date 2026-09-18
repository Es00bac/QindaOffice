// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher_controller.h"

#include <QFileInfo>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Launcher {

LauncherController::LauncherController(QObject *parent)
    : QObject(parent)
    , m_apps(officeApps())
    , m_statusTimer(new QTimer(this))
{
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(6000);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        m_status.clear();
        emit statusChanged();
    });
    refresh();
}

QVariantList LauncherController::apps() const
{
    QVariantList list;
    for (const AppEntry &app : m_apps) {
        // A card for an app that is not installed says so rather than
        // looking clickable and doing nothing.
        list.append(QVariantMap{{QStringLiteral("id"), app.id},
                                {QStringLiteral("name"), app.name},
                                {QStringLiteral("role"), app.role},
                                {QStringLiteral("blurb"), app.blurb},
                                {QStringLiteral("newLabel"), app.newLabel},
                                {QStringLiteral("iconSource"), iconSourceFor(app)},
                                {QStringLiteral("available"), !resolveBinary(app).isEmpty()}});
    }
    return list;
}

QVariantList LauncherController::recents() const
{
    QVariantList list;
    for (const RecentEntry &entry : m_recents) {
        QString iconSource;
        for (const AppEntry &app : m_apps) {
            if (app.id == entry.appId) {
                iconSource = iconSourceFor(app);
                break;
            }
        }
        list.append(QVariantMap{{QStringLiteral("path"), entry.path},
                                {QStringLiteral("fileName"), entry.fileName},
                                {QStringLiteral("folder"), entry.folder},
                                {QStringLiteral("appId"), entry.appId},
                                {QStringLiteral("iconSource"), iconSource}});
    }
    return list;
}

void LauncherController::refresh()
{
    m_recents = recentDocuments();
    emit recentsChanged();
}

void LauncherController::launchApp(int index)
{
    if (index >= 0 && index < m_apps.size()) {
        start(m_apps.at(index), QString());
    }
}

void LauncherController::openRecent(int index)
{
    if (index < 0 || index >= m_recents.size()) {
        return;
    }
    const RecentEntry entry = m_recents.at(index);
    for (const AppEntry &app : m_apps) {
        if (app.id == entry.appId) {
            start(app, entry.path);
            return;
        }
    }
}

void LauncherController::openPathChosen(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return;
    }
    const QString owner = appIdForPath(path);
    for (const AppEntry &app : m_apps) {
        if (app.id == owner) {
            start(app, path);
            return;
        }
    }
}

void LauncherController::start(const AppEntry &app, const QString &path)
{
    if (launch(app, path)) {
        report(path.isEmpty() ? tr("Starting %1…").arg(app.name)
                              : tr("Opening %1…").arg(QFileInfo(path).fileName()));
        return;
    }
    // Fail visibly: a launcher that silently does nothing is the worst
    // possible front door.
    report(tr("Could not start %1 — is it installed?").arg(app.name));
}

void LauncherController::report(const QString &text)
{
    m_status = text;
    emit statusChanged();
    // The message is a receipt, not a state: clear it so a stale
    // "Starting QindaWrite…" does not sit there for the rest of the session.
    m_statusTimer->start();
}

} // namespace QindaOffice::Launcher
