# SPDX-License-Identifier: GPL-3.0-or-later

# office_ink (plan sections 6.4/7.4): pen stroke model with pressure and
# tilt, a builder with smoothing/interpolation filters, a QPainter
# renderer, a spatial index, and JSON persistence. Qt Gui only - the
# canvas widget lives in the app and forwards tablet events here.
add_library(office_ink STATIC
    src/ink_persistence.cpp
    src/ink_persistence.h
    src/spatial_grid.cpp
    src/spatial_grid.h
    src/stroke_builder.cpp
    src/stroke_builder.h
    src/stroke_model.cpp
    src/stroke_model.h
)

target_include_directories(office_ink
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(office_ink PUBLIC Qt6::Core Qt6::Gui)

qqo_set_strict_warnings(office_ink)

