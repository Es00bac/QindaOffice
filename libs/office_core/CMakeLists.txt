# SPDX-License-Identifier: GPL-3.0-or-later

# office_core: document base + bounded persistence + the headless LibreOffice
# bridge. Qt Core+Gui only (QUndoStack lives in Gui); never links Qt Widgets
# and never links the desktop's archives (AGENTS.md rules 8/9), so it builds
# and tests without the QindaQt desktop installed.
add_library(office_core STATIC
    src/atomic_file_store.cpp
    src/atomic_file_store.h
    src/document.cpp
    src/document.h
    src/json_settings.cpp
    src/json_settings.h
    src/libre_office_converter.cpp
    src/libre_office_converter.h
    src/paths.cpp
    src/paths.h
    src/plain_text_document.cpp
    src/plain_text_document.h
    src/recent_files.cpp
    src/recent_files.h
    src/recovery_journal.cpp
    src/recovery_journal.h
    src/restore_inventory.cpp
    src/restore_inventory.h
)

target_include_directories(office_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(office_core PUBLIC Qt6::Core Qt6::Gui)

qqo_set_strict_warnings(office_core)

install(TARGETS office_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice/office_core
    FILES_MATCHING PATTERN "*.h")
