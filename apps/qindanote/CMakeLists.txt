# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindanote
    main.cpp
    src/note_find_bar.cpp
    src/note_find_bar.h
    src/note_page_render.cpp
    src/note_page_render.h
    src/note_page_view.cpp
    src/note_page_view.h
    src/note_window.cpp
    src/note_window.h
    src/notebook.cpp
    src/notebook.h
)
target_link_libraries(qindanote PRIVATE office_shell office_ink)
target_compile_definitions(qindanote PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaNote"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindanote)
install(TARGETS qindanote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaNote.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
