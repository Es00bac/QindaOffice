# SPDX-License-Identifier: GPL-3.0-or-later

# base_engine: the QindaBase database engine (M49, D-049) — one .qbase
# SQLite file with the user's tables and QindaBase's qb_ system tables:
# field types and exact currency, table designs and schema plans (preview,
# rebuild, backup), relationships, captured changes for undo, datasheet
# reads and edits, the query designer's SQL, action queries, saved objects,
# CSV import/export, Access import through mdb-tools (tables with their
# design, relationships and select queries — M65), file fields, starter
# templates and report snapshots; M80 (D-089): validation rules and input
# masks, crosstab and make-table queries, declared parameters, the SQL
# view in Access SQL both ways, macros and where conditions, reports
# evaluated with exact totals, mailing labels and Access's XML; M86 (D-094):
# calculated fields, value lists, Allow Zero Length, Hyperlink and Large
# Number fields, the Indexes dialog, query wizards, union queries and
# subqueries, report intervals, running sums and page control, data macros,
# Access's XML import and linked tables.
# Qt Core + the SQLite C API only: no Gui, no Quick, no desktop archives
# (AGENTS.md rules 8/9), so every behaviour is testable offscreen.
#
# AGENT-NOTE (dependency): dev-db/sqlite with the session extension
# (SQLITE_ENABLE_SESSION + PREUPDATE_HOOK — Gentoo builds it that way) and
# column metadata; QindaTK links the same library. Configure fails with a
# clear message when the session API is missing, because undo is built on it.
find_package(SQLite3 REQUIRED)
include(CheckSymbolExists)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES SQLite::SQLite3)
set(CMAKE_REQUIRED_DEFINITIONS -DSQLITE_ENABLE_SESSION -DSQLITE_ENABLE_PREUPDATE_HOOK)
check_symbol_exists(sqlite3session_create "sqlite3.h" QQO_SQLITE_HAS_SESSION)
check_symbol_exists(sqlite3_column_table_name "sqlite3.h" QQO_SQLITE_HAS_COLUMN_METADATA)
cmake_pop_check_state()
if(NOT QQO_SQLITE_HAS_SESSION OR NOT QQO_SQLITE_HAS_COLUMN_METADATA)
    message(FATAL_ERROR "QindaBase needs SQLite built with the session extension and column metadata "
                        "(SQLITE_ENABLE_SESSION, SQLITE_ENABLE_PREUPDATE_HOOK, SQLITE_ENABLE_COLUMN_METADATA).")
endif()

add_library(base_engine STATIC
    src/base_access.cpp
    src/base_access.h
    src/base_access_import.cpp
    src/base_access_import.h
    src/base_access_query.cpp
    src/base_access_query.h
    src/base_access_schema.cpp
    src/base_access_schema.h
    src/base_access_sql.h
    src/base_access_sql_columns.cpp
    src/base_access_sql_lex.cpp
    src/base_access_sql_p.h
    src/base_access_sql_parse.cpp
    src/base_access_sql_parser.h
    src/base_access_sql_print.cpp
    src/base_access_sql_statements.cpp
    src/base_access_sql_union.cpp
    src/base_calculated.cpp
    src/base_calculated.h
    src/base_changes.cpp
    src/base_csv.cpp
    src/base_csv.h
    src/base_data_macro.cpp
    src/base_data_macro.h
    src/base_expression.cpp
    src/base_expression.h
    src/base_expression_calls.cpp
    src/base_expression_lex.cpp
    src/base_expression_p.h
    src/base_expression_parser.h
    src/base_expression_sub.cpp
    src/base_file.cpp
    src/base_file.h
    src/base_file_field.cpp
    src/base_file_field.h
    src/base_form_controls.cpp
    src/base_form_controls.h
    src/base_functions.cpp
    src/base_hyperlink.cpp
    src/base_hyperlink.h
    src/base_indexes.cpp
    src/base_indexes.h
    src/base_input_mask.cpp
    src/base_input_mask.h
    src/base_labels.cpp
    src/base_labels.h
    src/base_links.cpp
    src/base_links.h
    src/base_macro.cpp
    src/base_macro.h
    src/base_money.cpp
    src/base_objects.cpp
    src/base_objects.h
    src/base_query.h
    src/base_query_compile.cpp
    src/base_query_core.cpp
    src/base_query_crosstab.cpp
    src/base_query_maketable.cpp
    src/base_query_p.h
    src/base_query_run.cpp
    src/base_query_union.cpp
    src/base_query_wizards.cpp
    src/base_query_wizards.h
    src/base_records.cpp
    src/base_records.h
    src/base_records_edit.cpp
    src/base_records_file.cpp
    src/base_report.cpp
    src/base_report.h
    src/base_report_keys.cpp
    src/base_report_p.h
    src/base_report_run.cpp
    src/base_report_totals.cpp
    src/base_rules.cpp
    src/base_rules.h
    src/base_schema.h
    src/base_schema_alter.cpp
    src/base_schema_maps.cpp
    src/base_schema_p.h
    src/base_schema_plan.cpp
    src/base_schema_read.cpp
    src/base_schema_rules.cpp
    src/base_schema_validate.cpp
    src/base_snapshot.cpp
    src/base_snapshot.h
    src/base_sqlite.cpp
    src/base_sqlite.h
    src/base_templates.cpp
    src/base_templates.h
    src/base_types.cpp
    src/base_types.h
    src/base_xml_export.cpp
    src/base_xml_export.h
    src/base_xml_import.cpp
    src/base_xml_import.h
)

target_include_directories(base_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(base_engine PUBLIC Qt6::Core PRIVATE SQLite::SQLite3)
target_compile_definitions(base_engine PRIVATE QT_NO_CAST_FROM_ASCII)

qqo_set_strict_warnings(base_engine)

# M89 (D-097): a form's positioned controls — the stacked layout of a form
# saved before M89, lookup labels and keys, calculated controls
# (QindaTK FormDesigner, D-311).
target_sources(base_engine PRIVATE
    src/base_form_layout.cpp
    src/base_form_layout.h
)
