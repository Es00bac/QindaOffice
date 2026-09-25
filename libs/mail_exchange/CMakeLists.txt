# SPDX-License-Identifier: GPL-3.0-or-later

# mail_exchange (M43, D-044): Exchange Web Services and Microsoft Graph
# calendar adapters over QindaTK's LocalProductivityStore - host adapters
# with the CalDavProvider surface (QindaTK D-189), so the calendar views and
# the store need no change for Exchange or Microsoft 365 accounts.
# Qt Core/Network + QindaTK only: no Quick code, no desktop archives, no
# dependency on office_mail (secrets arrive through setSecret()).
qt_add_library(mail_exchange STATIC
    src/calendar_sync.cpp
    src/calendar_sync.h
    src/ews_backend.cpp
    src/ews_backend.h
    src/ews_backend_p.h
    src/ews_calendar_provider.cpp
    src/ews_calendar_provider.h
    src/ews_mapping.cpp
    src/ews_mapping.h
    src/ews_soap.cpp
    src/ews_soap.h
    src/ews_writes.cpp
    src/exchange_common.cpp
    src/exchange_common.h
    src/graph_backend.cpp
    src/graph_backend.h
    src/graph_calendar_provider.cpp
    src/graph_calendar_provider.h
    src/graph_mapping.cpp
    src/graph_mapping.h
    src/graph_writes.cpp
    src/hosted_calendar_provider.cpp
    src/hosted_calendar_provider.h
    src/http_transport.cpp
    src/http_transport.h
    src/recurrence_pattern.cpp
    src/recurrence_pattern.h
    src/windows_zones.cpp
    src/windows_zones.h
    src/xml_tree.cpp
    src/xml_tree.h
)

target_include_directories(mail_exchange
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mail_exchange
    PUBLIC
        Qt6::Core
        Qt6::Network
        QindaTK::qindatk
)

qqo_set_strict_warnings(mail_exchange)
