// SPDX-License-Identifier: GPL-3.0-or-later
// QindaQt Office application entry point. All startup behaviour lives in
// office_shell's runOfficeApp (desktop identity pinning, CLI parsing,
// single-instance hand-off, recovery/restore startup); QQO_DESKTOP_ID is the
// org.qindaqt.<App> id from this app's .desktop file.

#include "src/show_window.h"

#include "app_runner.h"

int main(int argc, char *argv[])
{
    return QindaOffice::Shell::runOfficeApp(argc, argv, QStringLiteral(QQO_DESKTOP_ID),
                                            [] { return new QindaOffice::Apps::Show::ShowWindow; });
}
