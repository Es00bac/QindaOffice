# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_calc_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE calc_engine Qt6::Test)
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_calc_test(tst_calc_parser)
qqo_add_calc_test(tst_calc_functions)
qqo_add_calc_test(tst_calc_recalc)
qqo_add_calc_test(tst_calc_numfmt)
qqo_add_calc_test(tst_calc_csv)
