# SPDX-License-Identifier: GPL-3.0-or-later

# office_ooxml (M60, D-068): the Office Open XML foundation every native
# Microsoft Office mapping in the suite shares — the OPC package reader and
# writer (zip + content types + relationships), a namespace-aware XML tree
# whose fragments are the storage form for content preserved verbatim, the
# relationship copier that carries preserved fragments between packages,
# DrawingML themes and colours, units, and document properties. Qt Core/Gui
# + KArchive; no Quick, no desktop archives (AGENTS.md rule 8).
add_library(office_ooxml STATIC
    src/ooxml_copier.cpp
    src/ooxml_copier.h
    src/ooxml_namespaces.cpp
    src/ooxml_namespaces.h
    src/ooxml_package.cpp
    src/ooxml_package.h
    src/ooxml_properties.cpp
    src/ooxml_properties.h
    src/ooxml_theme.cpp
    src/ooxml_theme.h
    src/ooxml_units.h
    src/ooxml_xml.cpp
    src/ooxml_xml.h
)

target_include_directories(office_ooxml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(office_ooxml PUBLIC Qt6::Core Qt6::Gui KF6::Archive)

qqo_set_strict_warnings(office_ooxml)
