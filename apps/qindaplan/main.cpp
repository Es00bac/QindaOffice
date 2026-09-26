// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/plan_host.h"

#include "app_runner.h"

#include <QUrl>

// One window per project (D-051): each window is a PlanHost over its own
// PlanDocument; `qindaplan plan.qplan` or `qindaplan project.xml` (Microsoft
// Project XML) opens that project.
int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Plan/App/PlanWindow.qml")),
        [] { return new QindaOffice::Apps::Plan::PlanHost; });
}
