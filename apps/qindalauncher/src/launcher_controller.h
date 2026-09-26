// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The launcher's QML seam (conversion plan §5.5): the suite's apps and the
// suite's recent documents as plain maps, plus the three things the window
// can do — start an app, open a recent document, open a chosen path.
//
// AGENT-CONTRACT: the launcher READS the other apps' state and never
// writes it, and it starts them DETACHED — closing the front door must not
// take the documents with it.

#include "launcher_catalog.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QTimer;

namespace QindaOffice::Launcher {

class LauncherController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    // [{id, name, role, blurb, newLabel, iconSource, available}]
    Q_PROPERTY(QVariantList apps READ apps CONSTANT)
    // [{path, fileName, folder, appId, iconSource}], newest first
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    // A receipt ("Starting QindaWrite…"), cleared after a few seconds.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList openNameFilters READ openNameFilters CONSTANT)
public:
    explicit LauncherController(QObject *parent = nullptr);

    [[nodiscard]] QVariantList apps() const;
    [[nodiscard]] QVariantList recents() const;
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QStringList openNameFilters() const { return Launcher::openNameFilters(); }

    // Re-reads the per-app recent lists (the window does it when it shows).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void launchApp(int index);
    Q_INVOKABLE void openRecent(int index);
    Q_INVOKABLE void openPathChosen(const QUrl &url);

signals:
    void recentsChanged();
    void statusChanged();

private:
    void start(const AppEntry &app, const QString &path);
    void report(const QString &text);

    QList<AppEntry> m_apps;
    QList<RecentEntry> m_recents;
    QString m_status;
    QTimer *m_statusTimer;
};

} // namespace QindaOffice::Launcher
