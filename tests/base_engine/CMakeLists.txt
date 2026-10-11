# SPDX-License-Identifier: GPL-3.0-or-later

# base_engine tests (M49): headless, each in its own temp dir (and its own
# XDG_STATE_HOME, where automatic backups go). The engine is Qt Core +
# SQLite only, so these tests link no Gui.
function(qqo_add_base_test name)
    add_executable(${name} ${name}.cpp base_fixture.h)
    target_link_libraries(${name} PRIVATE base_engine Qt6::Test ${ARGN})
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_base_test(tst_base_types)
qqo_add_base_test(tst_base_file)
qqo_add_base_test(tst_base_schema)
qqo_add_base_test(tst_base_records)
qqo_add_base_test(tst_base_query)
qqo_add_base_test(tst_base_io)
qqo_add_base_test(tst_base_access)
# M80 (D-089): rules and masks, the new query kinds, the Access SQL view,
# macros, reports, labels and XML.
qqo_add_base_test(tst_base_rules)
qqo_add_base_test(tst_base_query_m80)
qqo_add_base_test(tst_base_access_sql)
qqo_add_base_test(tst_base_macro_report)
qqo_add_base_test(tst_base_macro_flow)
# M86 (D-094): calculated fields, value lists, Allow Zero Length, Hyperlink
# and Large Number; the Indexes dialog; query wizards, unions and
# subqueries; report intervals, levels, running sums and page control; data
# macros; Access's XML import and linked tables.
qqo_add_base_test(tst_base_fields_m86)
qqo_add_base_test(tst_base_indexes)
qqo_add_base_test(tst_base_query_m86)
qqo_add_base_test(tst_base_report_m86)
qqo_add_base_test(tst_base_data_macro)
qqo_add_base_test(tst_base_xml_links)
