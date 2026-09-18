// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_host.h"

#include "app_services.h"
#include "global_menu_export.h"
#include "preferences.h"
#include "standard_actions.h"

#include "recent_files.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>

namespace QindaOffice::Quick {

DocumentHost::DocumentHost(QObject *parent)
    : QObject(parent)
    , m_actions(new ActionModel(this))
    , m_menuTree(new MenuTree(this))
{
    addStandardActions(m_actions, appDisplayName());
    m_menuTree->setTopLevel(standardMenuTree());
    wireStandardCommands();

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        m_statusMessage.clear();
        emit statusMessageChanged();
    });

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setObjectName(QStringLiteral("autoSaveTimer"));
    connect(m_autoSaveTimer, &QTimer::timeout, this, &DocumentHost::autoSaveTick);

    // Every window of the app follows the store, so a change made in one
    // window reaches the others without a restart.
    connect(&Preferences::instance(), &Preferences::changed, this, [this] {
        applyChromePreferences();
        applyPreferences();
    });
    applyChromePreferences();
    syncTitle();
}

DocumentHost::~DocumentHost()
{
    // The export withdraws its announcement in its destructor; it must go
    // before the tree and model it observes.
    delete m_menuExport;
    m_menuExport = nullptr;
    if (m_window != nullptr) {
        m_window->deleteLater();
    }
}

QString DocumentHost::appDisplayName() const
{
    return QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("QindaOffice")
        : QGuiApplication::applicationDisplayName();
}

QString DocumentHost::appVersion() const
{
    return QCoreApplication::applicationVersion();
}

void DocumentHost::finalizeHost()
{
    if (m_finalized) {
        return;
    }
    m_finalized = true;
    const bool prints = static_cast<bool>(printRenderCallback());
    m_actions->setEnabled(QString::fromLatin1(ActionId::kFilePrint), prints);
    m_actions->setEnabled(QString::fromLatin1(ActionId::kFilePrintPreview), prints);
    applyPreferences();
}

PrintJob *DocumentHost::createPrintJob()
{
    PrintRenderCallback render = printRenderCallback();
    if (!render) {
        return nullptr;
    }
    delete m_printJob;
    m_printJob = new PrintJob(this);
    m_printJob->setRender(std::move(render));
    return m_printJob;
}

bool DocumentHost::exportPdf(const QString &path)
{
    PrintJob *job = createPrintJob();
    if (job == nullptr) {
        return false;
    }
    const bool ok = job->exportPdf(path);
    showStatusMessage(ok ? tr("Exported %1").arg(path)
                         : tr("Export failed: %1").arg(job->lastError()),
                      ok ? 5000 : 8000);
    return ok;
}

// ---- commands -------------------------------------------------------------

void DocumentHost::wireStandardCommands()
{
    using namespace ActionId;
    const auto on = [this](const char *id, auto &&slot) {
        connect(m_actions->entry(QString::fromLatin1(id)), &ActionEntry::triggered, this,
                std::forward<decltype(slot)>(slot));
    };
    on(kFileNew, [this] { emit newWindowRequested(); });
    on(kFileOpen, [this] {
        // Open next to the current document (or in Documents) rather than
        // in whatever directory the process happened to be launched from.
        const QString current = m_document != nullptr ? m_document->canonicalPath() : QString();
        const QString startDir = current.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : QFileInfo(current).absolutePath();
        emit openDialogRequested(startDir, openNameFilters());
    });
    on(kFileCloseWindow, [this] {
        if (m_window != nullptr) {
            m_window->close();
        }
    });
    on(kFileQuit, [this] {
        if (m_window != nullptr) {
            m_window->close();
        }
    });
    on(kFileSave, [this] { save(); });
    on(kFileSaveAs, [this] { saveAs(); });
    on(kSettingsPreferences, [this] { emit preferencesRequested(); });
    on(kHelpAbout, [this] { emit aboutRequested(); });
    on(kFilePrint, [this] { emit printRequested(); });
    on(kFilePrintPreview, [this] { emit printPreviewRequested(); });
    connect(m_actions->entry(QString::fromLatin1(kViewShowMenuBar)), &ActionEntry::toggled, this,
            [this](bool) { applyMenuBarVisibility(m_exported); });
    connect(m_actions->entry(QString::fromLatin1(kViewShowToolBars)), &ActionEntry::toggled, this,
            [this](bool show) {
                if (!m_applyingChromePrefs) {
                    Preferences::instance().setShowToolBars(show);
                }
                emit chromeChanged();
            });
    connect(m_actions->entry(QString::fromLatin1(kViewShowStatusBar)), &ActionEntry::toggled, this,
            [this](bool show) {
                if (!m_applyingChromePrefs) {
                    Preferences::instance().setShowStatusBar(show);
                }
                emit chromeChanged();
            });
    connect(m_actions->entry(QString::fromLatin1(kSettingsRestorePrevious)), &ActionEntry::toggled,
            this, [this](bool checked) {
                if (m_services != nullptr) {
                    m_services->setRestorePreviousDocuments(checked);
                }
            });
}

bool DocumentHost::toolBarsVisible() const
{
    return m_actions->entry(QString::fromLatin1(ActionId::kViewShowToolBars))->isChecked();
}

bool DocumentHost::statusBarVisible() const
{
    return m_actions->entry(QString::fromLatin1(ActionId::kViewShowStatusBar))->isChecked();
}

// ---- window ---------------------------------------------------------------

void DocumentHost::attachWindow(QQuickWindow *window)
{
    m_window = window;
    if (window == nullptr) {
        return;
    }
    // QWindow::setParent takes a window; the QObject parent is what ties
    // the window's lifetime to the host.
    static_cast<QObject *>(window)->setParent(this);
    bindShortcuts();
    // The export needs a mapped surface (Wayland announcement); start it on
    // the first show and keep it for the window's lifetime.
    connect(window, &QQuickWindow::visibleChanged, this, [this](bool visible) {
        if (visible && m_menuExport == nullptr) {
            startMenuExport();
        }
    });
    if (window->isVisible()) {
        startMenuExport();
    }
}

void DocumentHost::bindShortcuts()
{
    for (ActionEntry *entry : m_actions->entries()) {
        bindShortcut(entry);
    }
    connect(m_actions, &ActionModel::entryAdded, this,
            [this](const QString &id) { bindShortcut(m_actions->entry(id)); });
}

void DocumentHost::bindShortcut(ActionEntry *entry)
{
    if (entry == nullptr || m_window == nullptr || entry->shortcut().isEmpty()) {
        return;
    }
    // AGENT-NOTE: QtGui's QShortcut (a QWindow parent, WindowShortcut
    // context) goes through QShortcutMap, which asks the focused item with
    // ShortcutOverride first — so a text field keeps typing "P" while the
    // Pen tool's "P" works everywhere else. A key filter on the window
    // would steal the letter from every editor.
    auto *shortcut = new QShortcut(entry->shortcut(), m_window, nullptr, nullptr,
                                   Qt::WindowShortcut);
    shortcut->setEnabled(entry->isEnabled() && entry->isVisible());
    connect(shortcut, &QShortcut::activated, entry, &ActionEntry::trigger);
    connect(entry, &ActionEntry::changed, shortcut, [entry, shortcut] {
        shortcut->setEnabled(entry->isEnabled() && entry->isVisible());
        if (shortcut->key() != entry->shortcut()) {
            shortcut->setKey(entry->shortcut());
        }
    });
    m_shortcuts.append(shortcut);
}

void DocumentHost::startMenuExport()
{
    if (m_window == nullptr || m_menuExport != nullptr) {
        return;
    }
    m_menuExport = new GlobalMenuExport(m_menuTree, m_actions, m_window, this);
    connect(m_menuExport, &GlobalMenuExport::exportedChanged, this,
            &DocumentHost::applyMenuBarVisibility);
    applyMenuBarVisibility(m_menuExport->start());
}

void DocumentHost::applyMenuBarVisibility(bool exported)
{
    // Fail-closed rule: only a confirmed announcement hides the local bar;
    // the View action keeps local control either way.
    m_exported = exported;
    const bool showLocal =
        !exported && m_actions->entry(QString::fromLatin1(ActionId::kViewShowMenuBar))->isChecked();
    if (showLocal == m_localMenuVisible) {
        return;
    }
    m_localMenuVisible = showLocal;
    emit localMenuVisibleChanged();
}

// ---- engine ---------------------------------------------------------------

void DocumentHost::setDocument(Core::Document *document)
{
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
        delete m_document;
    }
    m_document = document;
    if (m_document != nullptr) {
        m_document->setParent(this);
        connect(m_document, &Core::Document::dirtyChanged, this, [this] { syncTitle(); });
        connect(m_document, &Core::Document::contentChanged, this,
                [this] { scheduleJournalSnapshot(); });
    }
    // A host that owns a document can save (D-011).
    m_actions->setEnabled(QString::fromLatin1(ActionId::kFileSave), m_document != nullptr);
    m_actions->setEnabled(QString::fromLatin1(ActionId::kFileSaveAs), m_document != nullptr);
    syncTitle();
    emit documentChanged();
}

// ---- services -------------------------------------------------------------

void DocumentHost::setAppServices(AppServices *services)
{
    m_services = services;
    ActionEntry *restore = m_actions->entry(QString::fromLatin1(ActionId::kSettingsRestorePrevious));
    restore->setEnabled(services != nullptr);
    if (services != nullptr) {
        restore->setChecked(services->restorePreviousDocuments());
        if (Core::RecentFiles *recent = services->recentFiles(); recent != nullptr) {
            connect(recent, &Core::RecentFiles::changed, this, [this] { rebuildRecentMenu(); });
        }
    }
    rebuildRecentMenu();
}

bool DocumentHost::restorePreviousDocuments() const
{
    return m_services != nullptr && m_services->restorePreviousDocuments();
}

void DocumentHost::setRestorePreviousDocuments(bool enabled)
{
    m_actions->entry(QString::fromLatin1(ActionId::kSettingsRestorePrevious))->setChecked(enabled);
}

void DocumentHost::rebuildRecentMenu()
{
    Core::RecentFiles *recent = m_services != nullptr ? m_services->recentFiles() : nullptr;
    MenuNode menu = MenuNode::submenu(QStringLiteral("fileRecentMenu"), tr("Open &Recent"));
    if (recent != nullptr) {
        const QStringList paths = recent->paths();
        // AGENT-GUARD: the entries are RECREATED on every open/save, so
        // their ids encode the list position — stable for the panel export
        // across rebuilds.
        int index = 0;
        for (const QString &path : paths) {
            const QString id = QStringLiteral("fileRecentItem%1").arg(index++);
            ActionEntry *entry = m_actions->add(id, QFileInfo(path).fileName());
            entry->setText(QFileInfo(path).fileName());
            entry->setVisible(true);
            entry->disconnect(this);
            connect(entry, &ActionEntry::triggered, this,
                    [this, path] { emit openPathsRequested({path}); });
            menu.children.append(MenuNode::action(id));
        }
        if (!paths.isEmpty()) {
            menu.children.append(MenuNode::separator());
            ActionEntry *clear = m_actions->add(QStringLiteral("fileRecentClear"), tr("Clear Menu"));
            clear->disconnect(this);
            connect(clear, &ActionEntry::triggered, this, [recent] { recent->clear(); });
            menu.children.append(MenuNode::action(QStringLiteral("fileRecentClear")));
        }
    }
    m_menuTree->replaceSubmenu(QStringLiteral("fileRecentMenu"), menu);
}

// ---- status ---------------------------------------------------------------

QString DocumentHost::title() const
{
    return m_document != nullptr ? m_document->displayTitle() : tr("No document");
}

bool DocumentHost::isDirty() const
{
    return m_document != nullptr && m_document->isDirty();
}

QString DocumentHost::windowTitle() const
{
    // No [*] here: QML windows show no modified mark of their own, so the
    // dirty state is spelled out with a bullet.
    return (isDirty() ? QStringLiteral("• ") : QString()) + title() + QStringLiteral(" — ")
        + appDisplayName();
}

void DocumentHost::syncTitle()
{
    emit titleChanged();
}

void DocumentHost::setStatusInfo(const QString &text)
{
    if (m_statusInfo == text) {
        return;
    }
    m_statusInfo = text;
    emit statusInfoChanged();
}

void DocumentHost::showStatusMessage(const QString &text, int timeoutMs)
{
    m_statusMessage = text;
    emit statusMessageChanged();
    if (timeoutMs > 0) {
        m_statusTimer->start(timeoutMs);
    } else {
        m_statusTimer->stop();
    }
}

void DocumentHost::setPreferencesPage(const QUrl &url)
{
    if (m_preferencesPage == url) {
        return;
    }
    m_preferencesPage = url;
    emit preferencesPageChanged();
}

// ---- preferences and auto-save -------------------------------------------

void DocumentHost::applyChromePreferences()
{
    // AGENT-GUARD: the toggles write back to the store, so applying the
    // store to them has to be marked or the two chase each other.
    m_applyingChromePrefs = true;
    const Preferences &prefs = Preferences::instance();
    m_actions->entry(QString::fromLatin1(ActionId::kViewShowToolBars))->setChecked(prefs.showToolBars());
    m_actions->entry(QString::fromLatin1(ActionId::kViewShowStatusBar))->setChecked(prefs.showStatusBar());
    m_applyingChromePrefs = false;
    emit chromeChanged();
    restartAutoSaveTimer();
}

void DocumentHost::restartAutoSaveTimer()
{
    const int minutes = Preferences::instance().autoSaveMinutes();
    if (minutes <= 0) {
        m_autoSaveTimer->stop();
        return;
    }
    m_autoSaveTimer->start(minutes * 60 * 1000);
}

QString DocumentHost::filterString() const
{
    return tr("Documents (*.txt *.md *.html *.htm);;All files (*)");
}

QStringList DocumentHost::openNameFilters() const
{
    return filterString().split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

QStringList DocumentHost::saveNameFilters() const
{
    return saveFilterString().split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

} // namespace QindaOffice::Quick
