// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaNote tests: the real NoteWindow.qml over a
// real NoteHost, offscreen with the software backend, plus tablet-event
// injection through QWindowSystemInterface (the path a driver uses, so
// the StylusHandler, mouse synthesis and grabs are all in play).

#include "qindanote/src/navigator_model.h"
#include "qindanote/src/note_host.h"
#include "qindanote/src/note_page_item.h"

#include <QGuiApplication>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtGui/private/qpointingdevice_p.h>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>

namespace NoteTest {

using QindaOffice::Apps::Notes::NoteHost;
using QindaOffice::Apps::Notes::NotePageItem;

inline QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    for (QQuickItem *child : parent->childItems()) {
        if (child->objectName() == name) {
            return child;
        }
        if (QQuickItem *found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

struct Scene {
    QQmlApplicationEngine engine;
    NoteHost host;
    QQuickWindow *window = nullptr;
    NotePageItem *page = nullptr;
    QString error;

    // A window at its declared size, or at `size` (a fixture width): the
    // size is set before show, as the suite itself does (ADR-0117).
    explicit Scene(const QSize &size = {})
    {
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Note/NoteWindow.qml")));
        if (component.isError()) {
            error = component.errorString();
            return;
        }
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("host"), QVariant::fromValue(&host)}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host.attachWindow(window);
        if (size.isValid()) {
            window->resize(size);
        }
        window->show();
        page = host.pageItem();
        // Tool state persists across windows (tools.json); every scene
        // starts from the pen so a test's eraser cannot leak into the next.
        if (page != nullptr) {
            page->setTool(QindaOffice::Apps::Notes::NoteTool::Pen);
        }
    }
    ~Scene() { delete window; }

    [[nodiscard]] bool ready() const { return window != nullptr && page != nullptr; }
    // Window coordinates of a point given in page (document) units.
    [[nodiscard]] QPointF at(const QPointF &pagePos) const { return page->mapToScene(pagePos); }
};

inline QPointingDevice *makeDevice(const QString &name, int id, QInputDevice::DeviceType type,
                                   QPointingDevice::PointerType pointer)
{
    auto *device = new QPointingDevice(name, id, type, pointer,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Pressure,
                                       1, 1);
    QPointingDevicePrivate::registerDevice(device);
    return device;
}

inline void tabletAt(QWindow *window, const QPointingDevice *device, const QPointF &pt,
                     Qt::MouseButtons buttons, qreal pressure)
{
    QWindowSystemInterface::handleTabletEvent(window, device, pt,
                                              QPointF(window->mapToGlobal(pt.toPoint())), buttons,
                                              pressure, 0.0, 0.0, 0.0, 0.0, 0, Qt::NoModifier);
    QWindowSystemInterface::flushWindowSystemEvents();
    QCoreApplication::processEvents();
}

// One pen stroke from `from` to `to` in WINDOW coordinates.
inline void tabletStroke(QWindow *window, const QPointingDevice *device, const QPointF &from,
                         const QPointF &to, Qt::MouseButtons extra = Qt::NoButton)
{
    tabletAt(window, device, from, Qt::LeftButton | extra, 0.8);
    tabletAt(window, device, (from + to) / 2, Qt::LeftButton | extra, 0.8);
    tabletAt(window, device, to, Qt::LeftButton | extra, 0.7);
    tabletAt(window, device, to, Qt::NoButton, 0.0);
}

// One finger dragged from `from` to `to` in WINDOW coordinates (a real
// touchscreen sequence through QWindowSystemInterface).
inline void fingerStroke(QWindow *window, QPointingDevice *touch, const QPointF &from,
                         const QPointF &to)
{
    QTest::touchEvent(window, touch).press(0, from.toPoint(), window);
    for (int i = 1; i <= 4; ++i) {
        QTest::touchEvent(window, touch).move(0, (from + (to - from) * i / 4.0).toPoint(), window);
    }
    QTest::touchEvent(window, touch).release(0, to.toPoint(), window);
    QCoreApplication::processEvents();
}

// Mouse fallback stroke in WINDOW coordinates (QTest's mouse helpers).
inline void mouseStroke(QWindow *window, const QPointF &from, const QPointF &to)
{
    QTest::mousePress(window, Qt::LeftButton, {}, from.toPoint());
    QTest::mouseMove(window, ((from + to) / 2).toPoint());
    QTest::mouseMove(window, to.toPoint());
    QTest::mouseRelease(window, Qt::LeftButton, {}, to.toPoint());
}

} // namespace NoteTest

#define NOTE_TEST_MAIN(TestClass)                                                          \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir config;                                                              \
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());                                \
        qputenv("XDG_STATE_HOME", config.path().toUtf8());                                 \
        qputenv("XDG_DATA_HOME", config.path().toUtf8());                                  \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));      \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaNote"));           \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
