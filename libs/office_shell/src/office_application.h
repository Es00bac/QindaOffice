// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

namespace QindaOffice::Shell {

class DocumentWindow;

// GUI-thread owner of the process's ordinary document windows (mirrors
// container-wm/src/apps/text_editor/ui/editor_application.h per ADR-0110).
// One process hosts at most kMaxWindows windows; New always opens a new
// window, never a tab; the process exits when its last window closes
// (QApplication::quitOnLastWindowClosed, default true).
//
// M0 scope: window lifetime, the 32-window bound, CLI-path admission. M1
// adds single-instance hand-off, restore inventory and recovery journaling
// (office_core); window subclasses per app land with their engines.
class OfficeApplication final : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxWindows = 32;

    explicit OfficeApplication(QObject *parent = nullptr);
    ~OfficeApplication() override;

    // Creates, tracks and shows a new window. Returns nullptr (and emits
    // diagnosticReported) when the window bound is reached. `path` is the
    // canonical document path the window presents; empty = pristine untitled.
    [[nodiscard]] DocumentWindow *newWindow(const QString &path = {});
    [[nodiscard]] bool openPaths(const QStringList &paths, QString *diagnostic = nullptr);

    [[nodiscard]] QList<DocumentWindow *> windows() const;
    [[nodiscard]] int windowCount() const;

signals:
    void windowCreated(DocumentWindow *window);
    void diagnosticReported(const QString &message);

private:
    void pruneClosed();
    void onWindowDestroyed(QObject *object);

    QList<QPointer<DocumentWindow>> m_windows;
};

} // namespace QindaOffice::Shell
