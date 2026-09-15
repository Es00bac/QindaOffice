// SPDX-License-Identifier: GPL-3.0-or-later
// QindaWrite entry point: shared office-shell startup with the page-view
// window factory.

#include "src/write_window.h"

#include "app_runner.h"

int main(int argc, char *argv[])
{
    return QindaOffice::Shell::runOfficeApp(argc, argv, QStringLiteral(QQO_DESKTOP_ID),
                                            [] { return new QindaOffice::Apps::Write::WriteWindow; });
}
