# SPDX-License-Identifier: GPL-3.0-or-later

# office_math (M92, D-100): Office equations — the OMML model (m:oMathPara /
# m:oMath read and written back with everything unknown kept, D-068), a
# TeX-quality layout from the math font's OpenType MATH table, painting and
# high-DPI images, Word's linear format (UnicodeMath + Math AutoCorrect) and
# LaTeX in and out, and an editing API with a caret and undo. QindaWrite and
# QindaShow adapters follow this library milestone; it depends on office_ooxml only. Qt Core/Gui; no Quick,
# no desktop archives (AGENTS.md rule 8).
add_library(office_math STATIC
    src/math_alphabets.cpp
    src/math_alphabets.h
    src/math_box.cpp
    src/math_box.h
    src/math_build.cpp
    src/math_build_p.h
    src/math_editor.cpp
    src/math_editor.h
    src/math_editor_navigation.cpp
    src/math_font.cpp
    src/math_font.h
    src/math_keywords.cpp
    src/math_latex.h
    src/math_latex_commands.cpp
    src/math_latex_p.h
    src/math_latex_parse.cpp
    src/math_latex_write.cpp
    src/math_layout.cpp
    src/math_layout.h
    src/math_layout_atoms.cpp
    src/math_layout_frac.cpp
    src/math_layout_p.h
    src/math_layout_scripts.cpp
    src/math_layout_table.cpp
    src/math_linear.h
    src/math_linear_p.h
    src/math_linear_parse.cpp
    src/math_linear_structs.cpp
    src/math_linear_write.cpp
    src/math_model.cpp
    src/math_model.h
    src/math_omml.cpp
    src/math_omml.h
    src/math_position.cpp
    src/math_position.h
    src/math_props.cpp
    src/math_props.h
    src/math_props_p.h
    src/math_props_run.cpp
    src/math_render.cpp
    src/math_structures.cpp
    src/math_structures.h
    src/math_symbols.cpp
    src/math_symbols.h
    src/math_table.cpp
    src/math_table.h

)

target_include_directories(office_math
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(office_math PUBLIC office_ooxml Qt6::Core Qt6::Gui)

qqo_set_strict_warnings(office_math)
