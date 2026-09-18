# SPDX-License-Identifier: GPL-3.0-or-later

# office_ai (plan section 5.3; conversion plan §3.8): chat types, SSE
# parsing, HTTP providers (OpenAI-compatible chat/completions, Anthropic
# Messages) and CLI subprocess providers (claude/codex/kimi), the model
# catalog, the secret store, the AssistantController and the inline action
# flow, plus the QML module QindaOffice.Ai (AssistantPanel, ProposalDialog).
# Qt Core/Gui/Network/Qml only. The Qt Widgets panel and proposal dialog
# is QML only (D-031: the Widgets panel and proposal dialog are gone).
qt_add_library(office_ai STATIC
    src/anthropic_provider.cpp
    src/anthropic_provider.h
    src/assistant_controller.cpp
    src/assistant_controller.h
    src/chat_types.cpp
    src/chat_types.h
    src/cli_provider.cpp
    src/cli_provider.h
    src/document_context.h
    src/http_chat_provider.cpp
    src/http_chat_provider.h
    src/inline_action_flow.cpp
    src/inline_action_flow.h
    src/model_catalog.cpp
    src/model_catalog.h
    src/secret_store.cpp
    src/secret_store.h
    src/sse_parser.cpp
    src/sse_parser.h
)

set(office_ai_qml qml/AssistantPanel.qml qml/ProposalDialog.qml)
foreach(qml_file IN LISTS office_ai_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
qt_add_qml_module(office_ai
    URI QindaOffice.Ai
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK
    QML_FILES ${office_ai_qml}
)

target_include_directories(office_ai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(office_ai
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        office_core
    PRIVATE
        qt6keychain
)

qqo_set_strict_warnings(office_ai)
