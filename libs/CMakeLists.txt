# SPDX-License-Identifier: GPL-3.0-or-later
add_subdirectory(calc_engine)
add_subdirectory(calc_io)
add_subdirectory(office_core)
# Office Open XML foundation (M60, D-068): packages, XML, themes.
add_subdirectory(office_ooxml)
add_subdirectory(office_quick)
add_subdirectory(office_ai)
# Live audio to text for QindaNote's live notes (D-057).
add_subdirectory(office_speech)
add_subdirectory(office_mail)
add_subdirectory(office_ink)
add_subdirectory(show_model)
add_subdirectory(mail_exchange)
add_subdirectory(books_engine)
add_subdirectory(books_document)
add_subdirectory(books_io)
add_subdirectory(books_connect)
# QindaDiagram's engine (M50, D-050): drawings, stencils, routing, painter.
add_subdirectory(diagram_engine)
# QindaPlan (M51, D-051): the scheduling engine and its paged output.
add_subdirectory(plan_engine)
add_subdirectory(plan_output)
# QindaBase's database engine (M49, D-049).
add_subdirectory(base_engine)
