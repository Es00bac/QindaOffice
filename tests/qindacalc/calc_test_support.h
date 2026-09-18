// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaCalc tests: the real CalcWindow.qml over a
// real CalcHost, offscreen with the software backend.

#include "qindacalc/src/calc_host.h"
#include "qindacalc/src/spreadsheet_document.h"
#include "qindacalc/src/spreadsheet_item.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

namespace CalcTest {

using QindaOffice::Apps::QindaCalc::CalcHost;
using QindaOffice::Apps::QindaCalc::SpreadsheetDocument;
using QindaOffice::Apps::QindaCalc::SpreadsheetItem;

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
    CalcHost host;
    QQuickWindow *window = nullptr;
    SpreadsheetItem *grid = nullptr;
    SpreadsheetDocument *doc = nullptr;
    QString error;

    explicit Scene(const QSize &size = {})
    {
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Calc/CalcWindow.qml")));
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
        grid = host.grid();
        doc = host.spreadsheetDocument();
    }
    ~Scene() { delete window; }

    [[nodiscard]] bool ready() const { return window != nullptr && grid != nullptr; }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findItem(window->contentItem(), name); }
};

} // namespace CalcTest

#define CALC_TEST_MAIN(TestClass)                                                          \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir config;                                                              \
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());                                \
        qputenv("XDG_STATE_HOME", config.path().toUtf8());                                 \
        qputenv("XDG_DATA_HOME", config.path().toUtf8());                                  \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaCalc"));      \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaCalc"));           \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
