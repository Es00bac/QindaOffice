// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/books_host.h"

#include "app_runner.h"

#include <QUrl>

// One window per company file (D-047): each window is a BooksHost over its
// own BooksDocument; File ▸ New Company opens a window that shows the
// setup wizard, and `qindabooks file.qbooks` opens that company.
int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Books/App/BooksWindow.qml")),
        [] { return new QindaOffice::Apps::Books::BooksHost; });
}
