// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/mail_host.h"
#include "src/mail_service.h"

#include "app_runner.h"

#include <QUrl>

// Every main window of the process is a MailHost over the ONE MailService
// (D-042). Positional arguments (mailto:, webcal:, .ics, .eml) reach
// MailHost::openWithoutDocument through OfficeApplication::openPaths, also
// when a second launch hands them to the running instance.
int main(int argc, char *argv[])
{
    return QindaOffice::Quick::runOfficeApp(
        argc, argv, QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")),
        [] { return new QindaOffice::Apps::Mail::MailHost(&QindaOffice::Apps::Mail::MailService::shared()); });
}
