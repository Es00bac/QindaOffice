# SPDX-License-Identifier: GPL-3.0-or-later
# show_model gates: the model, renderer and shape clipboard codec
# (tst_show_model, which also drives LibreOffice when present), and the
# flat-ODP mapping on its own (tst_fodp: lossless round trip, files the
# pre-M62 writer produced, LibreOffice's styled output, structural
# validity), and native PowerPoint over a package built by hand
# (pptx_fixture*.h): the reader (tst_pptx_read) and the writer
# (tst_pptx_write: equal round trips, schema validation when
# QQO_OOXML_SCHEMA_DIR is set, PowerPoint's id/relationship rules,
# LibreOffice's PDF of what we write).
foreach(name IN ITEMS tst_show_model tst_fodp tst_pptx_read tst_pptx_write)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE show_model office_ooxml Qt6::Test Qt6::Gui)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE
        QQO_SHOW_TEMPLATE_DIR="${PROJECT_SOURCE_DIR}/data/templates/QindaShow")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
