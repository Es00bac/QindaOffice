// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaShow tests: the real ShowWindow.qml over a
// real ShowHost, offscreen with the software backend.

#include "qindashow/src/show_document.h"
#include "qindashow/src/show_host.h"
#include "qindashow/src/slide_canvas_item.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

namespace ShowTest {

using QindaOffice::Apps::Show::ShowDocument;
using QindaOffice::Apps::Show::ShowHost;
using QindaOffice::Apps::Show::SlideCanvasItem;

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
    ShowHost host;
    QQuickWindow *window = nullptr;
    SlideCanvasItem *canvas = nullptr;
    ShowDocument *doc = nullptr;
    QString error;

    explicit Scene(const QSize &size = {})
    {
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Show/ShowWindow.qml")));
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
        canvas = host.canvas();
        doc = host.showDocument();
    }
    ~Scene() { delete window; }

    [[nodiscard]] bool ready() const { return window != nullptr && canvas != nullptr; }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findItem(window->contentItem(), name); }
    [[nodiscard]] QQuickWindow *childWindow(const QString &name) const
    {
        return window->findChild<QQuickWindow *>(name);
    }
};

} // namespace ShowTest

#define SHOW_TEST_MAIN(TestClass)                                                          \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir config;                                                              \
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());                                \
        qputenv("XDG_STATE_HOME", config.path().toUtf8());                                 \
        qputenv("XDG_DATA_HOME", config.path().toUtf8());                                  \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaShow"));      \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaShow"));           \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
