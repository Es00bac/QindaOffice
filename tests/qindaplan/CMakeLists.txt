# SPDX-License-Identifier: GPL-3.0-or-later
# QindaPlan's gates over the app (M51): PlanService/PlanHost over the real
# document with the seeded gala (tst_qindaplan_service), the panel export
# through the desktop's own validator (tst_qindaplan_menu_export) and the
# real window — views, ribbon gate, fixtures, grabs (tst_qindaplan_ui).
# M79 (D-088): Project's scheduling, tracking and views through the host
# (tst_qindaplan_tracking) and the real window (tst_qindaplan_views).
# M83 (D-092): task types, custom fields, splits, recurring tasks, the import
# and calendar recurrences through the host (tst_qindaplan_m83) and the real
# window (tst_qindaplan_m83_ui).
foreach(name IN ITEMS tst_qindaplan_service tst_qindaplan_menu_export tst_qindaplan_ui
                      tst_qindaplan_tracking tst_qindaplan_views tst_qindaplan_m83 tst_qindaplan_m83_ui)
    add_executable(${name} ${name}.cpp plan_app_test_support.h)
    target_link_libraries(${name} PRIVATE qindaplan_app qindaplan_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures"
                                               QQO_PLAN_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/plan_engine/data")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
