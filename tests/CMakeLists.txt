# Test binaries follow the pinned mira house style: one plain main() per
# binary using tests/support/test.hpp; labels drive CI selection (docs/
# project/project-standards.md section 11).
add_executable(dependency_wiring_test smoke/dependency_wiring_test.cpp)
target_compile_features(dependency_wiring_test PUBLIC cxx_std_20)
mirage_enable_warnings(dependency_wiring_test)
# Mira::core is a PRIVATE dependency of mirage_mira_host, so the pinned
# include dirs no longer propagate transitively; the smoke test asserts the
# pinned version identity directly and needs them.
target_link_libraries(dependency_wiring_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mirador
        Mirage::platform
        Mira::core
)
add_test(NAME dependency_wiring_test COMMAND dependency_wiring_test)
set_tests_properties(dependency_wiring_test PROPERTIES
    LABELS "unit;smoke"
    TIMEOUT 60
)

add_executable(mira_host_test runtime/mira_host_test.cpp)
target_compile_features(mira_host_test PUBLIC cxx_std_20)
mirage_enable_warnings(mira_host_test)
target_link_libraries(mira_host_test
    PRIVATE
        Mirage::mira_host
        Mira::core
        Mira::simulator_adapter
)
add_test(NAME mira_host_test COMMAND mira_host_test)
set_tests_properties(mira_host_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)

add_executable(desktop_observation_test desktop/desktop_observation_test.cpp)
target_compile_features(desktop_observation_test PUBLIC cxx_std_20)
mirage_enable_warnings(desktop_observation_test)
target_link_libraries(desktop_observation_test PRIVATE Mirage::desktop)
add_test(NAME desktop_observation_test COMMAND desktop_observation_test)
set_tests_properties(desktop_observation_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

add_executable(provider_contract_test desktop/provider_contract_test.cpp)
target_compile_features(provider_contract_test PUBLIC cxx_std_20)
mirage_enable_warnings(provider_contract_test)
target_link_libraries(provider_contract_test PRIVATE Mirage::desktop)
add_test(NAME provider_contract_test COMMAND provider_contract_test)
set_tests_properties(provider_contract_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M2-06 on-demand observation assembly: DesktopEnvironment-only unit tests
# against the fake and partial-provider stub environments.
add_executable(observation_assembler_test desktop/observation_assembler_test.cpp)
target_compile_features(observation_assembler_test PUBLIC cxx_std_20)
mirage_enable_warnings(observation_assembler_test)
target_link_libraries(observation_assembler_test PRIVATE Mirage::desktop)
add_test(NAME observation_assembler_test COMMAND observation_assembler_test)
set_tests_properties(observation_assembler_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M2-02 Linux backend skeleton: exercised against a private headless Xvfb
# (DEC-015). Built wherever the X11 dev packages exist; CI installs them.
find_package(X11 QUIET)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ATSPI_TEST QUIET IMPORTED_TARGET atspi-2 gobject-2.0 gio-2.0)
endif()
if(X11_FOUND AND X11_Xtst_FOUND)
    add_executable(x11_backend_test platform/x11_backend_test.cpp)
    target_compile_features(x11_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(x11_backend_test)
    target_link_libraries(x11_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            ${X11_X11_LIB}
            ${X11_Xtst_LIB}
    )
    add_test(NAME x11_backend_test COMMAND x11_backend_test)
    set_tests_properties(x11_backend_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
    )
endif()

# M2-03 AT-SPI2 accessibility: private D-Bus session plus a wire-exact GDBus
# fake application and an Xvfb window for the id -> title mapping (DEC-015).
# The test drives X11 directly, so it is only registered where the X11 dev
# packages exist (same precondition as x11_backend_test).
if(ATSPI_TEST_FOUND AND X11_FOUND)
    add_executable(atspi_backend_test platform/atspi_backend_test.cpp)
    target_compile_features(atspi_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(atspi_backend_test)
    target_link_libraries(atspi_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            PkgConfig::ATSPI_TEST
            ${X11_X11_LIB}
    )
    add_test(NAME atspi_backend_test COMMAND atspi_backend_test)
    set_tests_properties(atspi_backend_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        # The fixture's GDBus dispatch threads trip TSan inside the
        # UNINSTRUMENTED system glib (its internal synchronization is
        # invisible to TSan; every reported race lives in libglib/libgio,
        # none in Mirage code). Suppress exactly those system libraries;
        # races touching Mirage frames still fail the test.
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
    )
endif()

# M2-06 observation end-to-end (DEC-015 topology, M2 exit condition): the
# MiraEnvironmentBinding observe() loop over the real Linux backends —
# private D-Bus session with the wire-exact AT-SPI2 fixture desktop plus a
# real Xvfb window. Same precondition as atspi_backend_test.
if(ATSPI_TEST_FOUND AND X11_FOUND)
    add_executable(observation_e2e_test integration/observation_e2e_test.cpp)
    target_compile_features(observation_e2e_test PUBLIC cxx_std_20)
    mirage_enable_warnings(observation_e2e_test)
    target_link_libraries(observation_e2e_test
        PRIVATE
            Mirage::mira_host
            Mirage::integration_mira
            Mirage::platform
            Mirage::desktop
            PkgConfig::ATSPI_TEST
            ${X11_X11_LIB}
    )
    add_test(NAME observation_e2e_test COMMAND observation_e2e_test)
    set_tests_properties(observation_e2e_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
    )
endif()

# M2-05 Application / Notification frontends (DEC-015): the application
# test drives a private XDG data tree with fixture desktop entries and
# per-application copies of the helper binary (globally unique process
# names for the /proc matching); the notification test runs a private
# D-Bus session with a wire-exact GDBus Notifications fixture. Both are
# built and registered only where the gio-unix dev packages exist (the
# same gating as the platform frontends themselves) and need no X
# display.
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GIO_TEST QUIET IMPORTED_TARGET gio-unix-2.0)
endif()
if(GIO_TEST_FOUND)
    add_executable(m205_process_helper platform/m205_process_helper.cpp)
    target_compile_features(m205_process_helper PUBLIC cxx_std_20)
    mirage_enable_warnings(m205_process_helper)

    add_executable(application_backend_test platform/application_backend_test.cpp)
    target_compile_features(application_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(application_backend_test)
    target_link_libraries(application_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
    )
    add_test(NAME application_backend_test COMMAND application_backend_test)
    set_tests_properties(application_backend_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        # The GIO libraries in the platform backend run internal worker
        # threads (same situation as the AT-SPI2 fixture); suppress exactly
        # the uninstrumented system glib races, keep Mirage frames reportable.
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
    )

    add_executable(notification_backend_test platform/notification_backend_test.cpp)
    target_compile_features(notification_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(notification_backend_test)
    target_link_libraries(notification_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            PkgConfig::GIO_TEST
    )
    add_test(NAME notification_backend_test COMMAND notification_backend_test)
    set_tests_properties(notification_backend_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
    )
endif()

add_executable(mira_binding_test integration/mira_binding_test.cpp)
target_compile_features(mira_binding_test PUBLIC cxx_std_20)
mirage_enable_warnings(mira_binding_test)
target_link_libraries(mira_binding_test
    PRIVATE
        Mirage::mira_host
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
)
add_test(NAME mira_binding_test COMMAND mira_binding_test)
set_tests_properties(mira_binding_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)

add_executable(desktop_provider_boundary_test integration/desktop_provider_boundary_test.cpp)
target_compile_features(desktop_provider_boundary_test PUBLIC cxx_std_20)
mirage_enable_warnings(desktop_provider_boundary_test)
target_link_libraries(desktop_provider_boundary_test
    PRIVATE
        Mirage::mira_host
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
)
add_test(NAME desktop_provider_boundary_test COMMAND desktop_provider_boundary_test)
set_tests_properties(desktop_provider_boundary_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)

add_executable(ipc_protocol_test runtime/ipc_protocol_test.cpp)
target_compile_features(ipc_protocol_test PUBLIC cxx_std_20)
mirage_enable_warnings(ipc_protocol_test)
target_link_libraries(ipc_protocol_test PRIVATE Mirage::ipc)
add_test(NAME ipc_protocol_test COMMAND ipc_protocol_test)
set_tests_properties(ipc_protocol_test PROPERTIES
    LABELS "unit;protocol"
    TIMEOUT 60
)

add_executable(ipc_protocol_golden_test runtime/ipc_protocol_golden_test.cpp)
target_compile_features(ipc_protocol_golden_test PUBLIC cxx_std_20)
mirage_enable_warnings(ipc_protocol_golden_test)
# mira/json.hpp is a PRIVATE dependency of mirage_ipc (the public protocol
# surface never leaks the pinned JSON model), so the golden-vector gate links
# Mira::core explicitly to parse the shared vectors file.
target_link_libraries(ipc_protocol_golden_test PRIVATE Mirage::ipc Mira::core)
target_compile_definitions(ipc_protocol_golden_test PRIVATE
    MIRAGE_GOLDEN_VECTORS_JSON="${CMAKE_SOURCE_DIR}/tests/runtime/data/ipc_protocol_golden.json"
)
add_test(NAME ipc_protocol_golden_test COMMAND ipc_protocol_golden_test)
set_tests_properties(ipc_protocol_golden_test PROPERTIES
    LABELS "unit;protocol"
    TIMEOUT 60
)

add_executable(runtime_service_test runtime/runtime_service_test.cpp)
target_compile_features(runtime_service_test PUBLIC cxx_std_20)
mirage_enable_warnings(runtime_service_test)
target_link_libraries(runtime_service_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME runtime_service_test COMMAND runtime_service_test)
set_tests_properties(runtime_service_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)

add_executable(event_subscription_test runtime/event_subscription_test.cpp)
target_compile_features(event_subscription_test PUBLIC cxx_std_20)
mirage_enable_warnings(event_subscription_test)
target_link_libraries(event_subscription_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME event_subscription_test COMMAND event_subscription_test)
set_tests_properties(event_subscription_test PROPERTIES
    LABELS "integration"
    # 900 s: the deterministic overflow burst tolerates the registered
    # serial backlog pathology on 2-core runners (submit responses can lag
    # tens of seconds behind a 240 s shared deadline); normal runs finish in
    # well under a minute.
    TIMEOUT 900
)

add_executable(provider_hardening_test desktop/provider_hardening_test.cpp)
target_compile_features(provider_hardening_test PUBLIC cxx_std_20)
mirage_enable_warnings(provider_hardening_test)
target_link_libraries(provider_hardening_test
    PRIVATE
        Mirage::desktop
        Mirage::platform
)
add_test(NAME provider_hardening_test COMMAND provider_hardening_test)
set_tests_properties(provider_hardening_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)

add_executable(permission_test runtime/permission_test.cpp)
target_compile_features(permission_test PUBLIC cxx_std_20)
mirage_enable_warnings(permission_test)
target_link_libraries(permission_test PRIVATE Mirage::permission)
add_test(NAME permission_test COMMAND permission_test)
set_tests_properties(permission_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

add_executable(persistence_test runtime/persistence_test.cpp)
target_compile_features(persistence_test PUBLIC cxx_std_20)
mirage_enable_warnings(persistence_test)
target_link_libraries(persistence_test PRIVATE Mirage::persistence)
add_test(NAME persistence_test COMMAND persistence_test)
set_tests_properties(persistence_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

add_executable(recovery_service_test runtime/recovery_service_test.cpp)
target_compile_features(recovery_service_test PUBLIC cxx_std_20)
mirage_enable_warnings(recovery_service_test)
target_link_libraries(recovery_service_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
        Mirage::persistence
)
add_test(NAME recovery_service_test COMMAND recovery_service_test)
set_tests_properties(recovery_service_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)

add_executable(task_permission_test runtime/task_permission_test.cpp)
target_compile_features(task_permission_test PUBLIC cxx_std_20)
mirage_enable_warnings(task_permission_test)
target_link_libraries(task_permission_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME task_permission_test COMMAND task_permission_test)
set_tests_properties(task_permission_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)

add_executable(task_cancel_test runtime/task_cancel_test.cpp)
target_compile_features(task_cancel_test PUBLIC cxx_std_20)
mirage_enable_warnings(task_cancel_test)
target_link_libraries(task_cancel_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME task_cancel_test COMMAND task_cancel_test)
set_tests_properties(task_cancel_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)

# M1.5-03 dev bridge: the codec tests are pure byte machinery; the relay
# tests run a real DevBridgeLoop on the Executor against a scripted echo
# upstream over a temporary Unix socket. Both link only mirage_devbridge_core
# (executor headers come through its SYSTEM include dir, executor symbols
# through its LINK_ONLY interface).
add_executable(websocket_test devbridge/websocket_test.cpp)
target_compile_features(websocket_test PUBLIC cxx_std_20)
mirage_enable_warnings(websocket_test)
target_link_libraries(websocket_test PRIVATE mirage_devbridge_core)
add_test(NAME websocket_test COMMAND websocket_test)
set_tests_properties(websocket_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

add_executable(bridge_relay_test devbridge/bridge_relay_test.cpp)
target_compile_features(bridge_relay_test PUBLIC cxx_std_20)
mirage_enable_warnings(bridge_relay_test)
target_link_libraries(bridge_relay_test PRIVATE mirage_devbridge_core)
add_test(NAME bridge_relay_test COMMAND bridge_relay_test)
set_tests_properties(bridge_relay_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)
