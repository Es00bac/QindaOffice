// SPDX-License-Identifier: GPL-3.0-or-later
#include "office_application.h"

#include "document_window.h"

#include <QApplication>

#include <algorithm>

namespace QindaOffice::Shell {

OfficeApplication::OfficeApplication(QObject *parent)
    : QObject(parent)
{
}

OfficeApplication::~OfficeApplication() = default;

DocumentWindow *OfficeApplication::newWindow(const QString &path)
{
    if (m_windows.size() >= kMaxWindows) {
        emit diagnosticReported(
            tr("Window limit reached: %1 documents are already open in this process.")
                .arg(kMaxWindows));
        return nullptr;
    }
    auto *window = new DocumentWindow;
    if (!path.isEmpty()) {
        // M0: present the path in the title only; M1 hands it to the engine.
        window->setWindowTitle(
            tr("%1 — %2").arg(path, qAppName()));
    } else {
        window->setWindowTitle(qAppName());
    }
    m_windows.append(window);
    connect(window, &DocumentWindow::newWindowRequested, this,
            [this] { static_cast<void>(newWindow()); });
    connect(window, &DocumentWindow::destroyed, this, &OfficeApplication::onWindowDestroyed);
    window->show();
    emit windowCreated(window);
    return window;
}

bool OfficeApplication::openPaths(const QStringList &paths, QString *diagnostic)
{
    bool allOpened = true;
    for (const QString &path : paths) {
        if (newWindow(path) == nullptr) {
            allOpened = false;
            if (diagnostic != nullptr) {
                *diagnostic = tr("Could not open all documents: window limit reached.");
            }
            break;
        }
    }
    return allOpened;
}

QList<DocumentWindow *> OfficeApplication::windows() const
{
    QList<DocumentWindow *> result;
    result.reserve(m_windows.size());
    for (const QPointer<DocumentWindow> &window : m_windows) {
        if (window != nullptr) {
            result.append(window.data());
        }
    }
    return result;
}

// AGENT-NOTE: QPointer clears after the destroyed signal returns, so a
// destroyed-handler prune can still see a stale non-null pointer. Counts
// therefore always filter nulls instead of trusting prune timing.
int OfficeApplication::windowCount() const
{
    int live = 0;
    for (const QPointer<DocumentWindow> &window : m_windows) {
        if (window != nullptr) {
            ++live;
        }
    }
    return live;
}

void OfficeApplication::pruneClosed()
{
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
                                   [](const QPointer<DocumentWindow> &w) { return w.isNull(); }),
                    m_windows.end());
}

void OfficeApplication::onWindowDestroyed(QObject *object)
{
    Q_UNUSED(object);
    pruneClosed();
}

} // namespace QindaOffice::Shell
