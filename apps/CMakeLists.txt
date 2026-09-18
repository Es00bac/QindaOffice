# SPDX-License-Identifier: GPL-3.0-or-later

# One ordinary top-level window per document (ADR-0110); the desktop's
# containers provide tabs/splits. Each app is a thin main() over
# office_shell; document engines land per milestone (M1+).
function(qqo_add_app target desktopId)
    add_executable(${target} main.cpp)
    target_link_libraries(${target} PRIVATE office_shell)
    target_compile_definitions(${target} PRIVATE
        QQO_DESKTOP_ID="${desktopId}"
        QT_NO_CAST_FROM_ASCII
    )
    qqo_set_strict_warnings(${target})
    install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${PROJECT_SOURCE_DIR}/data/${desktopId}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endfunction()

add_subdirectory(qindanote)
add_subdirectory(qindacalc)
if(QQO_WIDGETS_APPS)
    add_subdirectory(qindawrite)
    add_subdirectory(qindashow)
    add_subdirectory(qindalauncher)
endif()
