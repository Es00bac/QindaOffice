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
# The Qt Widgets apps (QindaWrite, QindaCalc, QindaShow, the launcher) and
# office_shell build until their QindaTK conversion lands
# Qt 6.11 with the private Gui headers the per-window global-menu
# announcement builds on (version-locked like the desktop per ADR-0116;
# Qt6::GuiPrivate ships with qtbase's private-headers package).
find_package(KF6Archive REQUIRED)
find_package(KF6Sonnet REQUIRED)
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Concurrent Gui Widgets DBus Network PrintSupport Svg Test GuiPrivate
    Qml Quick QuickControls2 QuickTest QuickPrivate)
# QindaTK: the suite's UI toolkit (docs/plans/qindatk-conversion.md). A
# development checkout installs to <QindaTK>/install; point CMAKE_PREFIX_PATH
# at it, or at the system prefix the dev-libs/qindatk ebuild installs into.
find_package(QindaTK 0.1 REQUIRED)
# A source-only development dependency, never a home-directory installation.
set(QQO_AGENT_SOURCE_DIR "" CACHE PATH "QindaTK source checkout for an uninstalled agent SDK")
if(NOT TARGET QindaTK::agent)
    if(QQO_AGENT_SOURCE_DIR)
        add_subdirectory("${QQO_AGENT_SOURCE_DIR}/src/agent" agent-sdk EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "QindaOffice requires QindaTK::agent; update dev-libs/qindatk or set QQO_AGENT_SOURCE_DIR for development")
    endif()
endif()
# Poppler renders the print preview from the very PDF the job would print.
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
qt_standard_project_setup(REQUIRES 6.8)

# QQO_WITH_CHARTS gates the chart FEATURE (the custom ChartItem renderer,
# D-036); no QtCharts build or runtime dependency exists anymore.

# QXlsx (MIT) is vendored as a git submodule (plan section 3): the suite's
# xlsx reader/writer for calc_io (M3). Vendored code is exempt from the
# source-shape rules; built as a static library here so a broken submodule
# is caught at M0 instead of at M3. Nothing links it until calc_io lands.
#
# AGENT-GUARD: BUILD_SHARED_LIBS is forced OFF for the vendored copy, as a
# NORMAL variable so it shadows the cache. Gentoo's cmake.eclass sets
# `BUILD_SHARED_LIBS ON CACHE BOOL ""` for every package it builds, which
# silently turned this into /usr/lib64/libQXlsxQt6.so.1 — a vendored
# library installed under the same SONAME a system-wide QXlsx would use,
# ready to collide with one. Static, it links into calc_io and installs
# nothing. EXCLUDE_FROM_ALL drops its install() rules too; QXlsx has no
# option to disable them, and its headers and CMake package config are of
# no use to anyone once it is a private implementation detail.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/QXlsx/QXlsx EXCLUDE_FROM_ALL)

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

# AGENT-GUARD: the four application icons MUST be installed into the hicolor
# theme. Every .desktop file carries `Icon=org.qindaqt.<App>` (shell rule 2)
# and the SVGs have always lived under data/icons/, but nothing installed
# them — so the panel launcher, the task switcher and the window containers
# had no icon to resolve for any app in the suite. Named by the desktop id so
# QIcon::fromTheme() and the freedesktop icon lookup both find them.
install(DIRECTORY data/icons/hicolor
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons
    FILES_MATCHING PATTERN "*.svg")
