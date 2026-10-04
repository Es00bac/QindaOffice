# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

# This focused boundary also builds without Office's UI/desktop dependencies.
# It compiles the actual production backend; fixtures never read a live bus.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(office_mail_native_secrets LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    include(CTest)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus Test)
    get_filename_component(mail_src "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/office_mail/src" ABSOLUTE)
    add_library(mail_secret_boundary STATIC
        "${mail_src}/secret_backend.cpp"
        "${mail_src}/imported_password_backend.cpp"
        "${mail_src}/imported_password_codec.cpp"
        "${mail_src}/imported_password_reader.cpp"
        "${mail_src}/imported_password_reader.h")
    target_include_directories(mail_secret_boundary PUBLIC "${mail_src}")
    target_link_libraries(mail_secret_boundary PUBLIC Qt6::Core Qt6::DBus PRIVATE qt6keychain)
    target_compile_definitions(mail_secret_boundary PRIVATE QT_NO_CAST_FROM_ASCII)
    target_compile_options(mail_secret_boundary PRIVATE -Wall -Wextra -Wpedantic -Werror=return-type)
    set(mail_secret_test_library mail_secret_boundary)
else()
    set(mail_secret_test_library office_mail)
endif()

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
add_executable(tst_imported_password_codec tst_imported_password_codec.cpp)
target_link_libraries(tst_imported_password_codec PRIVATE ${mail_secret_test_library} Qt6::Test)
add_test(NAME tst_imported_password_codec COMMAND tst_imported_password_codec)

add_executable(tst_imported_password_backend
    tst_imported_password_backend.cpp synthetic_secret_service.cpp synthetic_secret_service.h)
target_link_libraries(tst_imported_password_backend PRIVATE ${mail_secret_test_library} Qt6::DBus Qt6::Test)
add_test(NAME tst_imported_password_backend COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" --
    $<TARGET_FILE:tst_imported_password_backend>)
set_tests_properties(tst_imported_password_backend PROPERTIES TIMEOUT 30
    ENVIRONMENT "QQO_MAIL_PRIVATE_SECRET_TEST=1;QQO_MAIL_SECRET_BACKEND=memory")
