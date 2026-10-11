# SPDX-License-Identifier: GPL-3.0-or-later
# office_math gates (M92, D-100): OMML round trips on Word-shaped samples with
# kept unknowns and shared-math.xsd validation (tst_math_omml, the schema step
# needs QQO_OOXML_SCHEMA_DIR and xmllint), the OpenType MATH layout and the
# 1x/2x pixel sheets under QQO_GRAB_DIR (tst_math_layout), UnicodeMath and
# LaTeX round trips (tst_math_linear) and the editing API with undo
# (tst_math_editor).
foreach(name IN ITEMS tst_math_omml tst_math_layout tst_math_linear tst_math_editor)
    add_executable(${name} ${name}.cpp math_samples.h)
    target_link_libraries(${name} PRIVATE office_math office_ooxml Qt6::Test Qt6::Gui)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
