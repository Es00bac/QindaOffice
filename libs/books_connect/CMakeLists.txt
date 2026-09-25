# SPDX-License-Identifier: GPL-3.0-or-later

# books_connect: QindaBooks' online bank and payment connections (M48,
# D-048; docs/books.md "Bank and payment connections") — Mercury (bank
# transactions into the bank feed) and Stripe (balance history into a
# clearing account, payouts matched to the bank, payment links on
# invoices). Qt Core/Network over books_engine, tokens in the desktop
# keyring through QtKeychain. No Gui, no Quick, no desktop archives
# (AGENTS.md rules 8/9): every behavior is tested offscreen against fake
# servers on 127.0.0.1.
# AGENT-GUARD: never link office_mail here (it drags the QML/QindaTK mail
# stack into the bookkeeping libraries); connect_secrets.h is the small
# equivalent of its SecretBackend.
add_library(books_connect STATIC
    src/connect_http.cpp
    src/connect_http.h
    src/connect_secrets.cpp
    src/connect_secrets.h
    src/connect_settings.cpp
    src/connect_settings.h
    src/connections_controller.cpp
    src/connections_controller.h
    src/connections_controller_sync.cpp
    src/mercury_client.cpp
    src/mercury_client.h
    src/mercury_sync.cpp
    src/mercury_sync.h
    src/payout_matching.cpp
    src/payout_matching.h
    src/stripe_client.cpp
    src/stripe_client.h
    src/stripe_posting.cpp
    src/stripe_posting.h
    src/stripe_sync.cpp
    src/stripe_sync.h
)

target_include_directories(books_connect
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_definitions(books_connect PRIVATE QT_NO_CAST_FROM_ASCII)

target_link_libraries(books_connect
    PUBLIC
        books_engine
        Qt6::Core
        Qt6::Network
    PRIVATE
        # Same dependency as office_mail / office_ai (dev-libs/qtkeychain,
        # Qt 6 build); headers <qt6keychain/…>.
        qt6keychain
)

qqo_set_strict_warnings(books_connect)
