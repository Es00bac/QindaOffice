// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc entry point: the shared office-shell startup with the
// spreadsheet window factory.

#include "src/calc_window.h"

#include "app_runner.h"

int main(int argc, char *argv[])
{
    return QindaOffice::Shell::runOfficeApp(argc, argv, QStringLiteral(QQO_DESKTOP_ID),
                                            [] { return new QindaOffice::Apps::QindaCalc::CalcWindow; });
}
