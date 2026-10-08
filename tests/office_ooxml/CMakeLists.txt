# SPDX-License-Identifier: GPL-3.0-or-later
# office_ooxml gates: XML tree parse/serialise (namespaces, mc prefix lists,
# whitespace rule), package read/write round trip, relationship copying and
# DrawingML colour arithmetic. QQO_OOXML_SCHEMA_DIR (optional, the ECMA-376
# Transitional XSDs) enables schema validation in the format tests.
add_executable(tst_ooxml tst_ooxml.cpp)
target_link_libraries(tst_ooxml PRIVATE office_ooxml Qt6::Test)
qqo_set_strict_warnings(tst_ooxml)
add_test(NAME tst_ooxml COMMAND tst_ooxml)
set_tests_properties(tst_ooxml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
