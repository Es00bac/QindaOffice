// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Single-instance support: a second `qindawrite` invocation hands its
// document paths to the running process over the session bus and exits.
//
// AGENT-CONTRACT: the well-known name org.qindaqt.<App> is owned by the
// primary process. The D-Bus method OpenPaths takes one flat argument —
// the list of paths to open (no custom struct marshalling). main's parser
// and this wire shape must stay in sync. `--new-instance` bypasses this
// entirely. Ported from QindaQt_Apps/src/dbus/single_instance.{h,cpp}.

#include <QObject>
#include <QString>
#include <QStringList>

namespace QindaOffice::Quick {

class SingleInstance final : public QObject {
    Q_OBJECT
    // AGENT-CONTRACT: without this pin, Qt derives the D-Bus interface name
    // from the C++ namespace and the client's OpenPaths call would never
    // match. The interface is the constant org.qindaqt.App for every suite
    // app (Q_CLASSINFO must be static); the well-known SERVICE name carries
    // the per-app identity (org.qindaqt.QindaWrite...), and both sides of
    // the wire use the pair (service, interface) consistently.
    Q_CLASSINFO("D-Bus Interface", "org.qindaqt.App")
public:
    explicit SingleInstance(QString serviceName, QString objectPath,
                            QObject *parent = nullptr);

    // Tries to become the primary instance. When another process already
    // owns the service, the paths are forwarded to it, `handedOff` is set
    // and false is returned; the caller should exit cleanly (exit code 0).
    [[nodiscard]] bool acquireOrHandOff(const QStringList &paths, bool *handedOff);

    // Publishes the receiving side; call only after acquireOrHandOff
    // returned true with *handedOff == false.
    [[nodiscard]] bool publish();

    // D-Bus method (service org.qindaqt.<App>, interface org.qindaqt.App).
    Q_INVOKABLE bool OpenPaths(const QStringList &paths);

signals:
    void openPathsRequested(const QStringList &paths);

private:
    [[nodiscard]] bool invokeOpenPaths(const QStringList &paths) const;

    QString m_serviceName;
    QString m_objectPath;
    QString m_interfaceName;
    bool m_acquired = false;
};

} // namespace QindaOffice::Quick
