// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/diagram_host.h"

#include "app_runner.h"

#include <QUrl>

// One window per drawing (D-050): each window is a DiagramHost over its own
// DrawingDocument; File ▸ New Drawing opens another window, and
// `qindadiagram file.qdiagram` opens that drawing.
int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Diagram/App/DiagramWindow.qml")),
        [] { return new QindaOffice::Apps::Diagram::DiagramHost; });
}
