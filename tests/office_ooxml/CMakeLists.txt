# SPDX-License-Identifier: GPL-3.0-or-later
# office_ooxml gates: XML tree parse/serialise (namespaces, mc prefix lists,
# whitespace rule), package read/write round trip, relationship copying and
# DrawingML colour arithmetic. QQO_OOXML_SCHEMA_DIR (optional, the ECMA-376
# Transitional XSDs) enables schema validation in the format tests.
# M75 (D-084): tst_ooxml_entries — a writer's entries read back without a
# zip, media stored, PowerPoint's comment prefixes.
add_executable(tst_ooxml tst_ooxml.cpp)
target_link_libraries(tst_ooxml PRIVATE office_ooxml Qt6::Test)
target_include_directories(tst_ooxml PRIVATE ${PROJECT_SOURCE_DIR}/tests)
qqo_set_strict_warnings(tst_ooxml)
add_test(NAME tst_ooxml COMMAND tst_ooxml)
set_tests_properties(tst_ooxml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_ooxml_entries tst_ooxml_entries.cpp)
target_link_libraries(tst_ooxml_entries PRIVATE office_ooxml Qt6::Test)
qqo_set_strict_warnings(tst_ooxml_entries)
add_test(NAME tst_ooxml_entries COMMAND tst_ooxml_entries)
set_tests_properties(tst_ooxml_entries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
