# SPDX-License-Identifier: GPL-3.0-or-later

# office_ai (plan section 5.3): chat types, SSE parsing, HTTP providers
# (OpenAI-compatible chat/completions, Anthropic Messages) and CLI
# subprocess providers (claude/codex/kimi), the model catalog and the
# secret store. Qt Core/Gui/Network only; the panel and inline flows
# consume this library from the apps.
add_library(office_ai STATIC
    src/anthropic_provider.cpp
    src/assistant_panel.cpp
    src/assistant_panel.h
    src/inline_action_flow.cpp
    src/inline_action_flow.h
    src/anthropic_provider.h
    src/chat_types.cpp
    src/chat_types.h
    src/cli_provider.cpp
    src/cli_provider.h
    src/http_chat_provider.cpp
    src/http_chat_provider.h
    src/model_catalog.cpp
    src/model_catalog.h
    src/secret_store.cpp
    src/secret_store.h
    src/sse_parser.cpp
    src/sse_parser.h
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
        Qt6::Widgets
        office_core
    PRIVATE
        qt6keychain
)

qqo_set_strict_warnings(office_ai)

