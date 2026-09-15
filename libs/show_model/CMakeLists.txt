# SPDX-License-Identifier: GPL-3.0-or-later

# show_model (plan section 7.5): the QindaShow slide model (shapes, text
# runs, builds, transitions, notes) and the flat-ODP (.fodp) subset
# writer/reader - the one hand-written format mapping the plan allows (no
# C++ pptx/odp library exists; LibreOffice does the pptx side). Qt Gui +
# KArchive.
add_library(show_model STATIC
    src/fodp_reader.cpp
    src/fodp_reader.h
    src/fodp_writer.cpp
    src/fodp_writer.h
    src/shape_json.cpp
    src/shape_json.h
    src/presentation.cpp
    src/presentation.h
    src/slide_renderer.cpp
    src/slide_renderer.h
)

target_include_directories(show_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(show_model PUBLIC Qt6::Core Qt6::Gui KF6::Archive)

qqo_set_strict_warnings(show_model)

install(TARGETS show_model ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice/show_model
    FILES_MATCHING PATTERN "*.h")
