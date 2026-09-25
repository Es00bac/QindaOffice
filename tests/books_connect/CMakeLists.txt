# SPDX-License-Identifier: GPL-3.0-or-later

# books_connect tests (M48, docs/verification.md#m48-gates): Mercury and
# Stripe against a fake API on 127.0.0.1 (fake_bank_api.h, the documented
# response shapes), an in-memory keyring, TZ pinned so dates are stable.
# No real network.
foreach(name IN ITEMS tst_books_connect_mercury tst_books_connect_stripe)
    add_executable(${name} ${name}.cpp books_connect_fixture.h fake_bank_api.h)
    target_link_libraries(${name} PRIVATE books_connect Qt6::Test Qt6::Network)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QQO_BOOKS_SECRET_BACKEND=memory;TZ=UTC")
endforeach()
