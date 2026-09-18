// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/calc_host.h"

#include "app_runner.h"

#include <QUrl>

int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Calc/CalcWindow.qml")),
        [] { return new QindaOffice::Apps::QindaCalc::CalcHost; });
}
