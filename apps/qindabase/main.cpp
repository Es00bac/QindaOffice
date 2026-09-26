// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/base_host.h"

#include "app_runner.h"

#include <QUrl>

// One window per database file (D-049): each window is a BaseHost over its
// own BaseDocument; File ▸ New Database opens a window that offers the
// starter templates, and `qindabase file.qbase` (or any SQLite file) opens it.
int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Base/App/BaseWindow.qml")),
        [] { return new QindaOffice::Apps::Base::BaseHost; });
}
