// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared helpers for the diagram_engine tests: a drawing with one page and
// shorthand for adding shapes and connectors through the real ops.

#include "drawing.h"
#include "ops.h"

#include <QGuiApplication>
#include <QtTest>

namespace DiagramTest {

using namespace QindaOffice::Diagram;

inline QString s(const char *text)
{
    return QString::fromUtf8(text);
}

struct Fixture {
    Drawing drawing = newDrawing();
    Page &page() { return drawing.pages.first(); }
    QString shape(const char *type, double cx, double cy, const char *text = nullptr)
    {
        return Ops::addShape(drawing, page(), s(type), QPointF(cx, cy),
                             text != nullptr ? std::optional<QString>(s(text)) : std::nullopt);
    }
    QString connect(const QString &from, const QString &to, int fromPort = -1, int toPort = -1,
                    Routing routing = Routing::Orthogonal)
    {
        return Ops::addConnector(drawing, page(), Endpoint{from, fromPort, {}}, Endpoint{to, toPort, {}}, routing);
    }
};

} // namespace DiagramTest

#define DIAGRAM_TEST_MAIN(TestClass)                                                                                   \
    int main(int argc, char *argv[])                                                                                   \
    {                                                                                                                  \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                                                       \
        QGuiApplication app(argc, argv);                                                                               \
        TestClass test;                                                                                                \
        return QTest::qExec(&test, argc, argv);                                                                        \
    }
