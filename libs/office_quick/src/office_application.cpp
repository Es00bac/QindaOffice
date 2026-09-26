// SPDX-License-Identifier: GPL-3.0-or-later
#include "office_application.h"

#include "document_host.h"
#include "single_instance.h"

#include "json_settings.h"
#include "paths.h"
#include "recent_files.h"
#include "recovery_journal.h"
#include "restore_inventory.h"

#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

#include <algorithm>

namespace QindaOffice::Quick {

OfficeApplication::OfficeApplication(QObject *parent)
    : QObject(parent)
{
}

OfficeApplication::~OfficeApplication() = default;

void OfficeApplication::setHostFactory(HostFactory factory)
{
    m_hostFactory = std::move(factory);
}

void OfficeApplication::setWindowComponent(QQmlEngine *engine, const QUrl &url)
{
    m_engine = engine;
    m_windowUrl = url;
}

// Lazily builds the persisted stores so a bare OfficeApplication (tests,
// tool reuse) never touches the user's config/state directories.
void OfficeApplication::ensureStores()
{
    if (m_settings != nullptr) {
        return;
    }
    const QString leaf = Core::appLeaf();
    m_settings = std::make_unique<Core::JsonSettings>(
        Core::configDirFor(leaf) + QStringLiteral("/settings.json"));
    m_recent = std::make_unique<Core::RecentFiles>(this);
    static_cast<void>(m_recent->load(
        Core::configDirFor(leaf) + QStringLiteral("/recent.json"), nullptr));
    m_inventory = std::make_unique<Core::RestoreInventory>(
        Core::stateDirFor(leaf) + QStringLiteral("/open-documents-v1.json"));
    m_inventory->setEnabled(
        m_settings->value(QStringLiteral("restorePrevious"), false).toBool());
    m_journal = std::make_unique<Core::RecoveryJournal>(
        Core::stateDirFor(leaf) + QStringLiteral("/recovery"), this);
}

QString OfficeApplication::serviceName() const
{
    return QStringLiteral("org.qindaqt.") + Core::appLeaf();
}

bool OfficeApplication::acquirePrimary(const QStringList &initialPaths, bool *handedOff)
{
    m_single = std::make_unique<SingleInstance>(
        serviceName(), QStringLiteral("/org/qindaqt/") + Core::appLeaf(), this);
    const bool acquired = m_single->acquireOrHandOff(initialPaths, handedOff);
    if (acquired) {
        // A failed publish warns by itself and costs only the hand-off: a
        // second launch then opens its own window instead of this one's.
        static_cast<void>(m_single->publish());
        connect(m_single.get(), &SingleInstance::openPathsRequested, this,
                [this](const QStringList &paths) { static_cast<void>(openPaths(paths)); });
        return true;
    }
    return false;
}

DocumentHost *OfficeApplication::newWindow(const QString &path)
{
    if (m_hosts.size() >= kMaxWindows) {
        emit diagnosticReported(
            tr("Window limit reached: %1 documents are already open in this process.")
                .arg(kMaxWindows));
        return nullptr;
    }
    DocumentHost *host = createHost();
    if (host == nullptr) {
        return nullptr;
    }
    present(host);
    if (!path.isEmpty() && !host->loadPath(path)) {
        host->requestClose();
        return nullptr;
    }
    return host;
}

DocumentHost *OfficeApplication::createHost()
{
    DocumentHost *host = m_hostFactory != nullptr ? m_hostFactory() : new DocumentHost;
    wireHost(host);
    if (!instantiateWindow(host)) {
        host->deleteLater();
        pruneClosed();
        return nullptr;
    }
    return host;
}

bool OfficeApplication::instantiateWindow(DocumentHost *host)
{
    if (m_engine == nullptr || m_windowUrl.isEmpty()) {
        // Headless use (tests drive the host directly).
        return true;
    }
    QQmlComponent component(m_engine, m_windowUrl);
    if (component.isError()) {
        emit diagnosticReported(component.errorString());
        return false;
    }
    QObject *object = component.createWithInitialProperties(
        {{QStringLiteral("host"), QVariant::fromValue(host)}});
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (window == nullptr) {
        emit diagnosticReported(component.isError()
                                    ? component.errorString()
                                    : tr("%1 is not a window").arg(m_windowUrl.toString()));
        delete object;
        return false;
    }
    host->attachWindow(window);
    return true;
}

void OfficeApplication::wireHost(DocumentHost *host)
{
    m_hosts.append(host);
    host->setAppServices(this);
    connect(host, &DocumentHost::newWindowRequested, this,
            [this] { static_cast<void>(newWindow()); });
    connect(host, &DocumentHost::openPathsRequested, this,
            [this, host](const QStringList &paths) {
                static_cast<void>(openPaths(paths, nullptr, host));
            });
    // The closing path must be captured while the host is alive; the
    // host (and its window) are destroyed on the next event-loop pass so
    // the close event completes first.
    connect(host, &DocumentHost::closing, this, [this](DocumentHost *closingHost) {
        m_lastClosingPath = closingHost->document() != nullptr
            ? closingHost->document()->canonicalPath()
            : QString();
        closingHost->deleteLater();
    });
    connect(host, &QObject::destroyed, this, [this] { hostClosed(); });
}

void OfficeApplication::present(DocumentHost *host)
{
    if (QQuickWindow *window = host->window()) {
        window->show();
        window->requestActivate();
    }
    emit hostCreated(host);
}

void OfficeApplication::activate(DocumentHost *host)
{
    if (QQuickWindow *window = host->window()) {
        window->raise();
        window->requestActivate();
    }
}

QString OfficeApplication::canonical(const QString &path) const
{
    return QFileInfo(path).canonicalFilePath();
}

bool OfficeApplication::openPaths(const QStringList &paths, QString *diagnostic,
                                  DocumentHost *requestingHost)
{
    // A bare second launch hands off an empty path list: a fresh untitled
    // window rather than nothing.
    if (paths.isEmpty()) {
        return newWindow() != nullptr;
    }
    bool allOpened = true;
    for (const QString &path : paths) {
        const QString canon = canonical(path);
        bool alreadyOpen = false;
        if (!canon.isEmpty()) {
            for (DocumentHost *host : hosts()) {
                if (host->document() != nullptr && host->document()->canonicalPath() == canon) {
                    activate(host);
                    alreadyOpen = true;
                    break;
                }
            }
        }
        if (alreadyOpen) {
            continue;
        }
        DocumentHost *target = nullptr;
        if (requestingHost != nullptr && requestingHost->isPristineUntitled()) {
            target = requestingHost;
        }
        for (DocumentHost *host : hosts()) {
            if (target != nullptr) {
                break;
            }
            if (host->isPristineUntitled()) {
                target = host;
            }
        }
        if (target == nullptr) {
            if (m_hosts.size() >= kMaxWindows) {
                allOpened = false;
                if (diagnostic != nullptr) {
                    *diagnostic = tr("Could not open all documents: window limit reached.");
                }
                break;
            }
            target = createHost();
            if (target == nullptr) {
                allOpened = false;
                break;
            }
            present(target);
        }
        if (!target->loadPath(path)) {
            allOpened = false;
            continue;
        }
        // AGENT-GUARD: a path handed over by a second launch lands in an
        // EXISTING pristine window (ADR-0110); that window has to come to
        // the front, or the load looks like nothing happened while the
        // document sits behind the terminal on another workspace.
        activate(target);
    }
    if (allOpened) {
        persistWindowInventory();
    }
    return allOpened;
}

QList<DocumentHost *> OfficeApplication::hosts() const
{
    QList<DocumentHost *> result;
    result.reserve(m_hosts.size());
    for (const QPointer<DocumentHost> &host : m_hosts) {
        if (host != nullptr) {
            result.append(host.data());
        }
    }
    return result;
}

int OfficeApplication::windowCount() const
{
    return int(hosts().size());
}

void OfficeApplication::hostClosed()
{
    pruneClosed();
    updateInventoryOnClose(m_lastClosingPath);
    m_lastClosingPath.clear();
}

void OfficeApplication::pruneClosed()
{
    m_hosts.erase(std::remove_if(m_hosts.begin(), m_hosts.end(),
                                 [](const QPointer<DocumentHost> &h) { return h.isNull(); }),
                  m_hosts.end());
}

void OfficeApplication::updateInventoryOnClose(const QString &closedPath)
{
    // AGENT-GUARD: bounded persistence, default OFF (AGENTS.md rule 7):
    // with the policy off nothing is written at all.
    if (m_inventory == nullptr || !m_inventory->enabled()) {
        return;
    }
    Core::RestoreInventory::Snapshot snapshot;
    for (DocumentHost *host : hosts()) {
        if (host->document() != nullptr && !host->document()->canonicalPath().isEmpty()) {
            snapshot.paths.append(host->document()->canonicalPath());
        }
    }
    if (snapshot.paths.isEmpty() && !closedPath.isEmpty()) {
        snapshot.paths.append(closedPath); // the final close preserves its path
        snapshot.activeIndex = 0;
    }
    m_inventory->setSnapshot(snapshot);
    static_cast<void>(m_inventory->persist(nullptr));
}

void OfficeApplication::persistWindowInventory()
{
    updateInventoryOnClose(QString());
}

bool OfficeApplication::restorePreviousDocuments() const
{
    return m_inventory != nullptr && m_inventory->enabled();
}

void OfficeApplication::setRestorePreviousDocuments(bool enabled)
{
    ensureStores();
    m_inventory->setEnabled(enabled);
    m_settings->setValue(QStringLiteral("restorePrevious"), enabled);
    static_cast<void>(m_settings->save(nullptr));
    if (enabled) {
        persistWindowInventory();
    } else {
        m_inventory->setSnapshot({});
        static_cast<void>(m_inventory->persist(nullptr));
    }
}

void OfficeApplication::restoreFromInventory()
{
    ensureStores();
    if (m_inventory == nullptr || !m_inventory->enabled()) {
        return;
    }
    const Core::RestoreInventory::Snapshot snapshot = m_inventory->snapshot();
    if (snapshot.paths.isEmpty()) {
        return;
    }
    static_cast<void>(openPaths(snapshot.paths));
    const QList<DocumentHost *> open = hosts();
    if (!open.isEmpty()) {
        activate(open.at(qBound(0, snapshot.activeIndex, int(open.size()) - 1)));
    }
}

Core::RecoveryJournal *OfficeApplication::journal()
{
    ensureStores();
    return m_journal.get();
}

Core::RecentFiles *OfficeApplication::recentFiles()
{
    ensureStores();
    return m_recent.get();
}

bool OfficeApplication::isPathOpenInAnotherWindow(const QString &canonicalPath,
                                                  const DocumentHost *self) const
{
    for (DocumentHost *host : hosts()) {
        if (host == self || host->document() == nullptr) {
            continue;
        }
        if (host->document()->canonicalPath() == canonicalPath) {
            return true;
        }
    }
    return false;
}

QString OfficeApplication::allocateUntitledJournalKey()
{
    ensureStores();
    return m_journal->allocateUntitledKey();
}

QString OfficeApplication::journalKeyForPath(const QString &canonicalPath) const
{
    return Core::RecoveryJournal::keyForPath(canonicalPath);
}

void OfficeApplication::documentSaved(const QString &canonicalPath)
{
    if (m_recent != nullptr && !canonicalPath.isEmpty()) {
        m_recent->add(canonicalPath);
    }
}

} // namespace QindaOffice::Quick
