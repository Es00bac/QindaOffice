// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaWrite tests: the real WriteWindow.qml over
// a real WriteHost, offscreen with the software backend.

#include "qindawrite/src/page_text_control.h"
#include "qindawrite/src/page_text_item.h"
#include "qindawrite/src/write_document.h"
#include "qindawrite/src/write_host.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QtTest>

namespace WriteTest {

using QindaOffice::Apps::Write::PageTextControl;
using QindaOffice::Apps::Write::PageTextItem;
using QindaOffice::Apps::Write::WriteDocument;
using QindaOffice::Apps::Write::WriteHost;

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
    WriteHost host;
    QQuickWindow *window = nullptr;
    PageTextItem *page = nullptr;
    WriteDocument *doc = nullptr;
    QString error;

    explicit Scene(const QSize &size = {})
    {
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Write/WriteWindow.qml")));
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
        doc = host.writeDocument();
        if (page != nullptr) {
            page->forceActiveFocus();
        }
    }
    ~Scene() { delete window; }

    [[nodiscard]] bool ready() const { return window != nullptr && page != nullptr; }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findItem(window->contentItem(), name); }
    [[nodiscard]] PageTextControl *control() const { return host.control(); }
    [[nodiscard]] QQuickItem *viewport() const { return item(QStringLiteral("writeViewport")); }

    // Puts "hello world" into the document and selects "hello".
    QTextCursor selectFirstWord()
    {
        QTextCursor cursor(doc->textDocument());
        cursor.insertText(QStringLiteral("hello world"));
        QTextCursor selection(doc->textDocument());
        selection.setPosition(0);
        selection.setPosition(5, QTextCursor::KeepAnchor);
        host.control()->setTextCursor(selection);
        return selection;
    }
};

} // namespace WriteTest

#define WRITE_TEST_MAIN(TestClass)                                                         \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir config;                                                              \
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());                                \
        qputenv("XDG_STATE_HOME", config.path().toUtf8());                                 \
        qputenv("XDG_DATA_HOME", config.path().toUtf8());                                  \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));     \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaWrite"));          \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
