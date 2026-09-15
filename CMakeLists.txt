# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

# AGENT-NOTE: this version, the Portage ebuild version, and the git tag move
# together; the ebuild pins the commit it builds (pattern: QindaQt_Apps).
project(qindaoffice
    VERSION 0.1.0
    DESCRIPTION "QindaQt Office - word processor, spreadsheet, presentations and notes for the QindaQt desktop"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
include(CTest)

option(QQO_WITH_CHARTS "Build the Qt Charts integration (emerge dev-qt/qtcharts first)" OFF)
option(QQO_STRICT_WARNINGS "Enable the suite's strict warning set on first-party targets" OFF)

# Qt 6.11 with the private Gui headers the per-window global-menu
# announcement builds on (version-locked like the desktop per ADR-0116;
# Qt6::GuiPrivate ships with qtbase's private-headers package).
find_package(KF6Archive REQUIRED)
find_package(KF6Sonnet REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets DBus Network PrintSupport Svg Test GuiPrivate WidgetsPrivate)

if(QQO_WITH_CHARTS)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Charts)
endif()

# QXlsx (MIT) is vendored as a git submodule (plan section 3): the suite's
# xlsx reader/writer for calc_io (M3). Vendored code is exempt from the
# source-shape rules; built as a static library here so a broken submodule
# is caught at M0 instead of at M3. Nothing links it until calc_io lands.
add_subdirectory(third_party/QXlsx/QXlsx)

# Strict warnings are opt-in (QQO_STRICT_WARNINGS=ON in the dev preset) and are
# applied per first-party target via qqo_set_strict_warnings(); vendored
# code under third_party/ is exempt (see AGENTS.md).
function(qqo_set_strict_warnings target)
    if(NOT QQO_STRICT_WARNINGS)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Werror=return-type
            -Werror=uninitialized
            -Werror=delete-non-virtual-dtor
        )
    endif()
endfunction()

add_subdirectory(libs)
add_subdirectory(apps)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES data/mime/qindaoffice.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
