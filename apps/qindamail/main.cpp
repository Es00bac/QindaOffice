// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/launch_arguments.h"
#include "src/mail_host.h"
#include "src/mail_service.h"

#include "app_runner.h"

#include <QDir>
#include <QUrl>

#include <vector>

// Every main window of the process is a MailHost over the ONE MailService
// (D-042). Positional arguments (mailto:, webcal:, .ics, .eml) reach
// MailHost::openWithoutDocument through OfficeApplication::openPaths, also
// when a second launch hands them to the running instance.
//
// `--attach <path>` (repeatable, M48) is folded with the mailto: URL into
// one `qindamail-compose:` positional BEFORE the shared runner parses the
// command line (it knows only positional paths, and the hand-off to a
// running instance carries only those); launch_arguments.h has the
// contract.
int main(int argc, char *argv[])
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i) {
        arguments << QString::fromLocal8Bit(argv[i]);
    }
    const QStringList folded =
        QindaOffice::Apps::Mail::Launch::foldAttachArguments(arguments, QDir::currentPath());
    std::vector<QByteArray> storage{QByteArray(argv[0])};
    for (const QString &a : folded) {
        storage.push_back(a.toLocal8Bit());
    }
    std::vector<char *> rewritten;
    for (QByteArray &bytes : storage) {
        rewritten.push_back(bytes.data());
    }
    rewritten.push_back(nullptr);
    int count = int(storage.size());
    return QindaOffice::Quick::runOfficeApp(
        count, rewritten.data(), QStringLiteral(QQO_DESKTOP_ID),
        QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")),
        [] { return new QindaOffice::Apps::Mail::MailHost(&QindaOffice::Apps::Mail::MailService::shared()); });
}
