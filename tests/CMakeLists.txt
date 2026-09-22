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

# M4-01 Windows backend skeleton: exercised against the session's real
# interactive desktop (DEC-017 decision 9 — the CI windows runner's desktop
# capability is confirmed on the first PR run). Registered only where the
# Win32 APIs exist; the MinGW cross builds compile and link it, the CI
# windows job runs it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_executable(win32_backend_test platform/win32_backend_test.cpp)
    target_compile_features(win32_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_backend_test)
    target_link_libraries(win32_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            user32
            gdi32
    )
    add_test(NAME win32_backend_test COMMAND win32_backend_test)
    set_tests_properties(win32_backend_test PROPERTIES
        LABELS "integration;platform"
        TIMEOUT 120
    )

    # M4-02 UIA accessibility frontend: exercised against the session's real
    # interactive desktop through the fixture windows this test creates (the
    # same DEC-017 decision 9 discipline as win32_backend_test). UIA itself
    # is a Windows system component, so no SDK library beyond the ones the
    # platform target already carries is needed here.
    add_executable(uia_backend_test platform/uia_backend_test.cpp)
    target_compile_features(uia_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(uia_backend_test)
    target_link_libraries(uia_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            user32
            gdi32
    )
    add_test(NAME uia_backend_test COMMAND uia_backend_test)
    # 120s: the main process only touches the fixture's own HWNDs (fast);
    # the semantic/structural desktop-scan scenarios run in a disposable
    # --scan-probe child under a 45s TerminateProcess watchdog (one wedged
    # provider window can hang a live-tree UIA call past every bounded
    # mechanism), so the worst case is well under a minute. Anything slower
    # means an unexpected wedge in the main process itself and must fail.
    set_tests_properties(uia_backend_test PROPERTIES
        LABELS "integration;platform"
        TIMEOUT 120
    )

    # M4-03 Win32 clipboard + Windows process execution: exercised against
    # the session's real clipboard and a real cmd.exe child tree (the same
    # DEC-017 decision 9 discipline as win32_backend_test). No fixture
    # window is needed: the clipboard scenarios manufacture their states
    # through the raw Win32 API (empty it, inject a private format, hold it
    # open as a peer), the process scenarios run real commands under the
    # provider's job object. The clipboard half needs an interactive
    # session; the process half is unconditional.
    add_executable(win32_clipboard_process_test platform/win32_clipboard_process_test.cpp)
    target_compile_features(win32_clipboard_process_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_clipboard_process_test)
    target_link_libraries(win32_clipboard_process_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            user32
    )
    add_test(NAME win32_clipboard_process_test COMMAND win32_clipboard_process_test)
    # 120s covers the bounded-retry scenarios (two 5s clipboard-open
    # deadlines, the 3s pipe-holding descendant budget) with wide margin;
    # normal runs finish in well under a minute.
    set_tests_properties(win32_clipboard_process_test PROPERTIES
        LABELS "integration;platform"
        TIMEOUT 120
    )

    # M4-04 Windows application frontend: exercised against the session's
    # real Start Menu (fixture .lnk entries written into the per-user
    # Programs tree) and real helper processes, so no interactive desktop
    # is needed. The helper binary is copied to globally unique names per
    # fixture application (the m205 discipline) so the image-name matching
    # only ever sees the processes this test created. The fixture shortcuts
    # are created through the same COM surface the frontend reads them
    # with.
    add_executable(m404_process_helper platform/m404_process_helper.cpp)
    target_compile_features(m404_process_helper PUBLIC cxx_std_20)
    mirage_enable_warnings(m404_process_helper)
    target_link_libraries(m404_process_helper PRIVATE user32 shell32)

    add_executable(win32_application_test platform/win32_application_test.cpp)
    target_compile_features(win32_application_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_application_test)
    target_link_libraries(win32_application_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            user32
            ole32
    )
    # The test copies m404_process_helper.exe to its run-unique fixture names
    # from its own directory at startup (GetModuleFileNameW), so the helper
    # must exist whenever the test binary is built — including partial CI
    # builds that name only the test target.
    add_dependencies(win32_application_test m404_process_helper)
    add_test(NAME win32_application_test COMMAND win32_application_test)
    # 120s covers the stuck-instance deadline scenarios (a 1.5s cooperative
    # deadline plus the mid-cancel probe) with wide margin; normal runs
    # finish in a few seconds.
    set_tests_properties(win32_application_test PROPERTIES
        LABELS "integration;platform"
        TIMEOUT 120
    )

    # M4-05 Windows notification frontend: exercised against the session's
    # real notification area (DEC-018 carrier) — open() builds the hidden
    # carrier window and the resident tray icon, notify() posts real
    # balloons. The frozen refusal matrix is strict; the positive
    # acceptance scenarios note (never claim) balloon visibility, and a
    # session without a notification area skips them loudly instead of
    # failing (the DEC-018 verification plan records that possibility).
    add_executable(win32_notification_test platform/win32_notification_test.cpp)
    target_compile_features(win32_notification_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_notification_test)
    target_link_libraries(win32_notification_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
    )
    add_test(NAME win32_notification_test COMMAND win32_notification_test)
    set_tests_properties(win32_notification_test PROPERTIES
        LABELS "integration;platform"
        TIMEOUT 120
    )

    # M4-06 product processes on Windows: the real RuntimeService serving
    # a named pipe against the real IpcClient (the DEC-007 round trip the
    # milestone exit condition names), the shared golden-vector framing
    # identity, and the DEC-011 Windows store. Runs wherever the binary
    # runs; no interactive desktop needed.
    add_executable(win32_product_process_test platform/win32_product_process_test.cpp)
    target_compile_features(win32_product_process_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_product_process_test)
    target_link_libraries(win32_product_process_test
        PRIVATE
            Mirage::runtime_service
            Mirage::persistence
            Mirage::platform
            Mirage::desktop
    )
    add_test(NAME win32_product_process_test COMMAND win32_product_process_test)
    set_tests_properties(win32_product_process_test PROPERTIES
        LABELS "integration;platform"
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

    # M3-05 visual observation end-to-end (DEC-016, M3 exit condition): the
    # binding's wired visual surface over the real X11 capture — refresh
    # (fake backends on a real Executor blocking worker) -> ScreenFrame
    #Descriptor with a store-recorded payload -> perception evidence in
    # global desktop coordinates -> @vN click injection -> re-observe with a
    # fresh generation. Same precondition as observation_e2e_test.
    add_executable(visual_observation_e2e_test integration/visual_observation_e2e_test.cpp)
    target_compile_features(visual_observation_e2e_test PUBLIC cxx_std_20)
    mirage_enable_warnings(visual_observation_e2e_test)
    target_link_libraries(visual_observation_e2e_test
        PRIVATE
            Mirage::mira_host
            Mirage::integration_mirador
            Mirage::integration_mira
            Mirage::platform
            Mirage::desktop
            PkgConfig::ATSPI_TEST
            ${X11_X11_LIB}
            # The test asserts the fake backends' accepted-format identity
            # (pinned mirador::PixelFormat); mirador is a PRIVATE dependency
            # of mirage_integration_mirador, so the pinned include dirs must
            # be linked explicitly (same shape as fake_visual_backend_test).
            mirador::core
    )
    # The pipeline drives the pinned Executor directly (the per-source
    # blocking worker); headers are consumed as SYSTEM headers and the
    # library is linked by file path (same consumption form as the visual
    # unit tests).
    target_include_directories(visual_observation_e2e_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
    )
    target_link_libraries(visual_observation_e2e_test PRIVATE
        $<TARGET_FILE:executor::executor>
    )
    add_test(NAME visual_observation_e2e_test COMMAND visual_observation_e2e_test)
    set_tests_properties(visual_observation_e2e_test PROPERTIES
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
# POSIX-only: unistd fixtures.
if(UNIX)
target_compile_features(mira_binding_test PUBLIC cxx_std_20)
mirage_enable_warnings(mira_binding_test)
target_link_libraries(mira_binding_test
    PRIVATE
        Mirage::mira_host
        Mirage::integration_mirador
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        # The wired-binding scenarios configure the fake backends' accepted
        # format (pinned mirador::PixelFormat); mirador is a PRIVATE
        # dependency of mirage_integration_mirador, so the pinned include
        # dirs must be linked explicitly (same shape as
        # fake_visual_backend_test).
        mirador::core
)
endif()
# The M3-05 wired-binding scenarios run the concrete VisualObservationPipeline
# over the fake environment's screen provider; the pipeline drives the pinned
# Executor directly (the per-source blocking worker), consumed as SYSTEM
# headers and linked by file path (same form as the visual unit tests).
target_include_directories(mira_binding_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_link_libraries(mira_binding_test PRIVATE
    $<TARGET_FILE:executor::executor>
)
add_test(NAME mira_binding_test COMMAND mira_binding_test)
set_tests_properties(mira_binding_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)

add_executable(desktop_provider_boundary_test integration/desktop_provider_boundary_test.cpp)
# POSIX-only: signal self-pipe fixtures.
if(UNIX)
target_compile_features(desktop_provider_boundary_test PUBLIC cxx_std_20)
mirage_enable_warnings(desktop_provider_boundary_test)
target_link_libraries(desktop_provider_boundary_test
    PRIVATE
        Mirage::mira_host
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
)
endif()
add_test(NAME desktop_provider_boundary_test COMMAND desktop_provider_boundary_test)
set_tests_properties(desktop_provider_boundary_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)

add_executable(ipc_protocol_test runtime/ipc_protocol_test.cpp)
# POSIX-only: exercises the Unix-socket transport and a forked helper.
if(UNIX)
target_compile_features(ipc_protocol_test PUBLIC cxx_std_20)
mirage_enable_warnings(ipc_protocol_test)
target_link_libraries(ipc_protocol_test PRIVATE Mirage::ipc)
add_test(NAME ipc_protocol_test COMMAND ipc_protocol_test)
set_tests_properties(ipc_protocol_test PROPERTIES
    LABELS "unit;protocol"
    TIMEOUT 60
)
endif()

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
# POSIX-only: Unix-socket endpoints and a signal self-pipe; the Windows
# product-process round trip is win32_product_process_test.
if(UNIX)
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
endif()
add_test(NAME runtime_service_test COMMAND runtime_service_test)
set_tests_properties(runtime_service_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)

add_executable(event_subscription_test runtime/event_subscription_test.cpp)
# POSIX-only: same Unix-socket transport coupling as runtime_service_test.
if(UNIX)
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
endif()
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
# POSIX-only: asserts the 0700/0600 store hardening; the Windows store is
# covered by win32_product_process_test.
if(UNIX)
target_compile_features(persistence_test PUBLIC cxx_std_20)
mirage_enable_warnings(persistence_test)
target_link_libraries(persistence_test PRIVATE Mirage::persistence)
add_test(NAME persistence_test COMMAND persistence_test)
set_tests_properties(persistence_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)
endif()

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
# POSIX-only: signal-driven cancellation probes.
if(UNIX)
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
endif()
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
# POSIX-only: the devbridge transport (see apps/CMakeLists.txt).
if(UNIX)
target_compile_features(websocket_test PUBLIC cxx_std_20)
mirage_enable_warnings(websocket_test)
target_link_libraries(websocket_test PRIVATE mirage_devbridge_core)
add_test(NAME websocket_test COMMAND websocket_test)
set_tests_properties(websocket_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)
endif()

add_executable(bridge_relay_test devbridge/bridge_relay_test.cpp)
# POSIX-only: the devbridge transport (see apps/CMakeLists.txt).
if(UNIX)
target_compile_features(bridge_relay_test PUBLIC cxx_std_20)
mirage_enable_warnings(bridge_relay_test)
target_link_libraries(bridge_relay_test PRIVATE mirage_devbridge_core)
add_test(NAME bridge_relay_test COMMAND bridge_relay_test)
set_tests_properties(bridge_relay_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)
endif()

# M3-01 visual contract (DEC-016): VisualSnapshot render forms and the
# VisualReferenceRegistry publication contract, including a concurrent
# writer/reader atomicity scenario (a TSAN target).
add_executable(visual_contract_test desktop/visual_contract_test.cpp)
target_compile_features(visual_contract_test PUBLIC cxx_std_20)
mirage_enable_warnings(visual_contract_test)
target_link_libraries(visual_contract_test PRIVATE Mirage::desktop)
add_test(NAME visual_contract_test COMMAND visual_contract_test)
set_tests_properties(visual_contract_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M3-01 fake visual backends (DEC-016 decision 5): the deterministic OCR /
# detection stand-ins against the pinned mirador SPI obligations. mirador::core
# is a PRIVATE dependency of mirage_integration_mirador, so the pinned include
# dirs no longer propagate transitively; the test asserts mirador Status
# semantics directly and needs them (same shape as dependency_wiring_test).
add_executable(fake_visual_backend_test integration/fake_visual_backend_test.cpp)
target_compile_features(fake_visual_backend_test PUBLIC cxx_std_20)
mirage_enable_warnings(fake_visual_backend_test)
target_link_libraries(fake_visual_backend_test
    PRIVATE
        Mirage::integration_mirador
        mirador::core
)
add_test(NAME fake_visual_backend_test COMMAND fake_visual_backend_test)
set_tests_properties(fake_visual_backend_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M3-02 mirador integration adapter (DEC-016 decisions 3/4/5): the execution
# context and frame translations, the bounded backend registry and the
# latest-wins visual session host, including cancellation, supersede, stop
# settlement and first-frame scenarios on a real Executor blocking worker.
# mirador::fusion is a PRIVATE dependency of mirage_integration_mirador, so
# the pinned include dirs no longer propagate transitively; the test asserts
# mirador Status / geometry semantics directly and needs them.
add_executable(visual_session_test integration/visual_session_test.cpp)
target_compile_features(visual_session_test PUBLIC cxx_std_20)
mirage_enable_warnings(visual_session_test)
target_link_libraries(visual_session_test
    PRIVATE
        Mirage::integration_mirador
        mirador::fusion
)
# The session adapter drives the pinned Executor directly (the per-source
# blocking worker); headers are consumed as SYSTEM headers and the library is
# linked by file path (same consumption form as mirage_devbridge_core).
target_include_directories(visual_session_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_link_libraries(visual_session_test PRIVATE
    $<TARGET_FILE:executor::executor>
)
add_test(NAME visual_session_test COMMAND visual_session_test)
set_tests_properties(visual_session_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)

# M3-03 ElementTargetExecutor (DEC-005 resolution order, DEC-016 decision 6):
# ring-by-ring target resolution with traceable degraded fall-through, the
# pointer click sequence under cooperative cancellation, and fail-closed
# behavior for unbound dependencies, against the fake desktop environment.
add_executable(element_target_executor_test desktop/element_target_executor_test.cpp)
target_compile_features(element_target_executor_test PUBLIC cxx_std_20)
mirage_enable_warnings(element_target_executor_test)
target_link_libraries(element_target_executor_test PRIVATE Mirage::desktop)
add_test(NAME element_target_executor_test COMMAND element_target_executor_test)
set_tests_properties(element_target_executor_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M3-03 visual observation loop (DEC-016 decisions 2/3): the fusion ->
# VisualSnapshot mapping, publish_visual_snapshot boundary semantics and the
# session -> publish -> observe -> click closed loop over the fake backends
# on a real Executor (no X, no D-Bus). mirador::core is a PRIVATE dependency
# of mirage_integration_mirador, so the pinned include dirs no longer
# propagate transitively; the test asserts mirador geometry semantics
# directly and needs them (same shape as visual_session_test).
add_executable(visual_observation_loop_test integration/visual_observation_loop_test.cpp)
target_compile_features(visual_observation_loop_test PUBLIC cxx_std_20)
mirage_enable_warnings(visual_observation_loop_test)
target_link_libraries(visual_observation_loop_test
    PRIVATE
        Mirage::integration_mirador
        mirador::core
)
# The session host drives the pinned Executor directly (the per-source
# blocking worker); headers are consumed as SYSTEM headers and the library is
# linked by file path (same consumption form as visual_session_test).
target_include_directories(visual_observation_loop_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_link_libraries(visual_observation_loop_test PRIVATE
    $<TARGET_FILE:executor::executor>
)
add_test(NAME visual_observation_loop_test COMMAND visual_observation_loop_test)
set_tests_properties(visual_observation_loop_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)

# M3-04 visual template cache and geometry proposals (DEC-016 decision 3):
# the per-source VisualTemplateIndex over the pinned mirador VisualIndex
# (creation validation, clear-winner reuse policy, capacity/eviction
# reconciliation) and the session host's enrollment / probe / geometry stages
# on a real Executor blocking worker, including the template_id resolution
# closed loop into ElementTargetExecutor. mirador::fusion is a PRIVATE
# dependency of mirage_integration_mirador, so the pinned include dirs no
# longer propagate transitively; the test asserts mirador Status / geometry
# semantics directly and needs them (same shape as visual_session_test).
add_executable(visual_template_index_test integration/visual_template_index_test.cpp)
target_compile_features(visual_template_index_test PUBLIC cxx_std_20)
mirage_enable_warnings(visual_template_index_test)
target_link_libraries(visual_template_index_test
    PRIVATE
        Mirage::integration_mirador
        mirador::fusion
)
add_test(NAME visual_template_index_test COMMAND visual_template_index_test)
set_tests_properties(visual_template_index_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

add_executable(visual_cache_geometry_test integration/visual_cache_geometry_test.cpp)
target_compile_features(visual_cache_geometry_test PUBLIC cxx_std_20)
mirage_enable_warnings(visual_cache_geometry_test)
target_link_libraries(visual_cache_geometry_test
    PRIVATE
        Mirage::integration_mirador
        mirador::fusion
)
# The session host drives the pinned Executor directly (the per-source
# blocking worker); headers are consumed as SYSTEM headers and the library is
# linked by file path (same consumption form as visual_session_test).
target_include_directories(visual_cache_geometry_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_link_libraries(visual_cache_geometry_test PRIVATE
    $<TARGET_FILE:executor::executor>
)
add_test(NAME visual_cache_geometry_test COMMAND visual_cache_geometry_test)
set_tests_properties(visual_cache_geometry_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)
