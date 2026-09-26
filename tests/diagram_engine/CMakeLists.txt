# SPDX-License-Identifier: GPL-3.0-or-later

# diagram_engine tests (M50, D-050): model, file format and document undo
# (tst_diagram_model), edit operations (tst_diagram_ops), routing, layout,
# importers and output (tst_diagram_output). Headless (offscreen).
function(qqo_add_diagram_engine_test name)
    add_executable(${name} ${name}.cpp diagram_fixture.h)
    target_link_libraries(${name} PRIVATE diagram_engine Qt6::Test)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_diagram_engine_test(tst_diagram_model)
qqo_add_diagram_engine_test(tst_diagram_ops)
qqo_add_diagram_engine_test(tst_diagram_output)
