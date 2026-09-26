// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProcess>
#include <QProcessEnvironment>

namespace QindaOffice::Test {

// AGENT-CONTRACT: mirrors Core::LibreOfficeConverter::headlessEnvironment()
// (libs/office_core/src/libre_office_converter.cpp). Tests that start soffice
// themselves must use it: the ctest environment pins QT_QPA_PLATFORM=offscreen
// and a QindaQt session exports SAL_USE_VCLPLUGIN=qt6, and together they make
// soffice abort before converting. Kept header-only so tests of the Qt-Core
// libraries (calc_io, show_model) need not link office_core.
inline QProcessEnvironment sofficeEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SAL_USE_VCLPLUGIN"), QStringLiteral("svp"));
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    env.remove(QStringLiteral("QT_QUICK_BACKEND"));
    return env;
}

// Starts soffice with sofficeEnvironment().
inline void startSoffice(QProcess &process, const QStringList &arguments)
{
    process.setProcessEnvironment(sofficeEnvironment());
    process.start(QStringLiteral("soffice"), arguments);
}

} // namespace QindaOffice::Test
