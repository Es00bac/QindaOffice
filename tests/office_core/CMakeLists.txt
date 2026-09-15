# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_core_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_core Qt6::Widgets Qt6::Test)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Core tests are widget-free; offscreen keeps any accidental Gui use safe.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_core_test(tst_atomic_file_store)
qqo_add_core_test(tst_plain_text_document)
qqo_add_core_test(tst_recent_files)
qqo_add_core_test(tst_restore_inventory)
qqo_add_core_test(tst_recovery_journal)
qqo_add_core_test(tst_libre_office_converter)
