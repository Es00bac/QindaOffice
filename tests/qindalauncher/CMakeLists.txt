# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_launcher tst_launcher.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindalauncher/src/app_card.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindalauncher/src/launcher_catalog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindalauncher/src/launcher_window.cpp
)
target_link_libraries(tst_launcher PRIVATE Qt6::Widgets Qt6::Test office_core)
target_include_directories(tst_launcher PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_launcher)
add_test(NAME tst_launcher COMMAND tst_launcher)
set_tests_properties(tst_launcher PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
