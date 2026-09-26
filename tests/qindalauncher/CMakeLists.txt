# SPDX-License-Identifier: GPL-3.0-or-later
# The launcher's gates over the QtQuick front door: the catalog (which app
# opens a path, how the four recent lists merge) and the real
# LauncherWindow.qml over a real LauncherController.
add_executable(tst_launcher tst_launcher.cpp)
target_link_libraries(tst_launcher PRIVATE qindalauncher_app qindalauncher_appplugin Qt6::Test)
target_include_directories(tst_launcher PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
qqo_set_strict_warnings(tst_launcher)
qqo_desktop_runtime(tst_launcher)
add_test(NAME tst_launcher COMMAND tst_launcher)
set_tests_properties(tst_launcher PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
