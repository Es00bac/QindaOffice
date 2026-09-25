# SPDX-License-Identifier: GPL-3.0-or-later

# books_document: the thin Core::Document adapter that lets a QindaBooks
# window host a company file through office_quick's DocumentHost (D-045).
# Separate from books_engine because Core::Document links Qt Gui
# (QUndoStack) and the engine stays Qt Core + Sql only.
add_library(books_document STATIC
    src/books_document.cpp
    src/books_document.h
)

target_include_directories(books_document
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(books_document PUBLIC books_engine office_core)

qqo_set_strict_warnings(books_document)
