# Test binaries follow the pinned mira house style: one plain main() per
# binary using tests/support/test.hpp; labels drive CI selection (docs/
# project/project-standards.md section 11).
add_executable(native_chat_model_test desktop/native_chat_model_test.cpp
    ${CMAKE_SOURCE_DIR}/apps/native/chat_model.cpp ${CMAKE_SOURCE_DIR}/apps/native/attachment.cpp)
target_include_directories(native_chat_model_test PRIVATE ${CMAKE_SOURCE_DIR}/apps/native)
target_compile_features(native_chat_model_test PRIVATE cxx_std_20)
target_link_libraries(native_chat_model_test PRIVATE Mirage::conversation)
mirage_enable_warnings(native_chat_model_test)
add_test(NAME native_chat_model_test COMMAND native_chat_model_test)
set_tests_properties(native_chat_model_test PROPERTIES LABELS "unit;ui" TIMEOUT 30)

add_executable(tool_alias_provider_test integration/tool_alias_provider_test.cpp)
target_compile_features(tool_alias_provider_test PRIVATE cxx_std_20)
target_link_libraries(tool_alias_provider_test PRIVATE Mirage::integration_mira Mira::core)
mirage_enable_warnings(tool_alias_provider_test)
add_test(NAME tool_alias_provider_test COMMAND tool_alias_provider_test)
set_tests_properties(tool_alias_provider_test PROPERTIES LABELS "integration;runtime;protocol" TIMEOUT 30)

add_executable(credential_store_test platform/credential_store_test.cpp)
target_compile_features(credential_store_test PRIVATE cxx_std_20)
mirage_enable_warnings(credential_store_test)
target_link_libraries(credential_store_test PRIVATE Mirage::platform Mira::core)
target_include_directories(credential_store_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
target_link_libraries(credential_store_test PRIVATE $<TARGET_FILE:kairo::kairo>)
add_test(NAME credential_store_test COMMAND credential_store_test)
set_tests_properties(credential_store_test PROPERTIES LABELS "unit;platform" TIMEOUT 30)

add_executable(dependency_wiring_test smoke/dependency_wiring_test.cpp)
# POSIX-coupled: the scripted providers sleep through <poll.h>; Windows keeps
# its own platform validation instead of compiling these Linux-flow tests.
if(UNIX)
    add_executable(ppocr_pipeline_test integration/ppocr_pipeline_test.cpp)
    target_compile_features(ppocr_pipeline_test PRIVATE cxx_std_20)
    mirage_enable_warnings(ppocr_pipeline_test)
    target_link_libraries(ppocr_pipeline_test PRIVATE Mirage::integration_mirador
        Mirage::runtime_service Mira::core $<TARGET_FILE:kairo::kairo>)
    target_include_directories(ppocr_pipeline_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    add_test(NAME ppocr_pipeline_test COMMAND ppocr_pipeline_test)
    set_tests_properties(ppocr_pipeline_test PROPERTIES LABELS "integration;runtime" TIMEOUT 30)
endif()
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
# The workflow-surface scenario (M5-05) drives the pinned WorkflowRuntime on
# its own Executor instance: SYSTEM headers plus a by-file-path link, same
# form as the visual unit tests.
target_include_directories(mira_host_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(mira_host_test
    PRIVATE
        Mirage::mira_host
        Mira::core
        Mira::simulator_adapter
        $<TARGET_FILE:kairo::kairo>
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

# M5-09 Desktop Overlay frame contract (DEC-029): the budget vocabulary and
# the UTF-8-boundary clamp cut are platform-neutral; platform-neutral unit.
add_executable(overlay_surface_test desktop/overlay_surface_test.cpp)
target_compile_features(overlay_surface_test PUBLIC cxx_std_20)
mirage_enable_warnings(overlay_surface_test)
target_link_libraries(overlay_surface_test PRIVATE Mirage::desktop)
add_test(NAME overlay_surface_test COMMAND overlay_surface_test)
set_tests_properties(overlay_surface_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# Product child ownership, including the pet's absent/reaped shutdown path.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(frontend_process_test platform/frontend_process_test.cpp)
    target_link_libraries(frontend_process_test PRIVATE Mirage::platform Mirage::ipc kairo)
    target_include_directories(frontend_process_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    mirage_enable_warnings(frontend_process_test)
    add_test(NAME frontend_process_test COMMAND frontend_process_test)
    set_tests_properties(frontend_process_test PROPERTIES LABELS "unit;platform;lifecycle" TIMEOUT 20)
endif()

# M2-02 Linux backend skeleton: private headless Xvfb (DEC-015).
# Built wherever the X11 dev packages exist; CI installs them.
find_package(X11 QUIET)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ATSPI_TEST QUIET IMPORTED_TARGET atspi-2 gobject-2.0 gio-2.0)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
    add_executable(frontend_activation_test platform/frontend_activation_test.cpp)
    target_compile_features(frontend_activation_test PUBLIC cxx_std_20)
    mirage_enable_warnings(frontend_activation_test)
    target_include_directories(frontend_activation_test PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(frontend_activation_test PRIVATE Mirage::platform ${X11_X11_LIB})
    add_test(NAME frontend_activation_test COMMAND frontend_activation_test)
    set_tests_properties(frontend_activation_test PROPERTIES LABELS "integration;platform" TIMEOUT 20)
endif()

if(X11_FOUND AND X11_Xtst_FOUND)
    add_executable(x11_pointer_actions_test platform/x11_pointer_actions_test.cpp)
    target_compile_features(x11_pointer_actions_test PUBLIC cxx_std_20)
    target_include_directories(x11_pointer_actions_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    mirage_enable_warnings(x11_pointer_actions_test)
    target_link_libraries(x11_pointer_actions_test PRIVATE
        Mirage::platform Mirage::desktop Mirage::integration_mira Mirage::integration_mirador
        mirador::fusion kairo ${X11_X11_LIB} ${X11_Xtst_LIB})
    get_target_property(MIRAGE_X11_POINTER_HELPERS mirage_platform
                        MIRAGE_X11_DRAG_HELPERS_AVAILABLE)
    if(MIRAGE_X11_POINTER_HELPERS)
        target_compile_definitions(x11_pointer_actions_test PRIVATE MIRAGE_TEST_X11_DRAG_HELPERS=1)
        target_include_directories(x11_pointer_actions_test SYSTEM PRIVATE ${X11_XRes_INCLUDE_PATH})
        target_link_libraries(x11_pointer_actions_test PRIVATE ${X11_XRes_LIB})
    endif()
    add_test(NAME x11_pointer_actions_test COMMAND x11_pointer_actions_test)
    set_tests_properties(x11_pointer_actions_test PROPERTIES LABELS "integration;platform" TIMEOUT 30)
    # Opt-in application compatibility acceptance: GTK is an external test
    # process via system Python GI, never a product link/download dependency.
    add_executable(gtk_pointer_effects_test platform/gtk_pointer_effects_test.cpp)
    target_compile_features(gtk_pointer_effects_test PUBLIC cxx_std_20)
    target_include_directories(gtk_pointer_effects_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    mirage_enable_warnings(gtk_pointer_effects_test)
    target_link_libraries(gtk_pointer_effects_test PRIVATE Mirage::platform Mirage::desktop
        Mirage::integration_mira Mirage::integration_mirador mirador::fusion Mira::core kairo ${X11_X11_LIB} ${X11_Xtst_LIB})
    get_target_property(MIRAGE_X11_DRAG_HELPERS_AVAILABLE mirage_platform
                        MIRAGE_X11_DRAG_HELPERS_AVAILABLE)
    if(MIRAGE_X11_DRAG_HELPERS_AVAILABLE)
        target_compile_definitions(gtk_pointer_effects_test PRIVATE MIRAGE_TEST_X11_DRAG_HELPERS=1)
        target_include_directories(gtk_pointer_effects_test SYSTEM PRIVATE ${X11_XRes_INCLUDE_PATH})
        target_link_libraries(gtk_pointer_effects_test PRIVATE ${X11_XRes_LIB})
    endif()
    option(MIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE "Run external GTK3 pointer acceptance" OFF)
    if(MIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE)
        find_program(MIRAGE_GTK_TEST_PYTHON NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
        execute_process(COMMAND "${MIRAGE_GTK_TEST_PYTHON}" -c
            "import gi; gi.require_version('Gtk', '3.0'); from gi.repository import Gtk"
            RESULT_VARIABLE MIRAGE_GTK_TEST_RESULT)
        if(NOT MIRAGE_GTK_TEST_RESULT EQUAL 0)
            message(FATAL_ERROR "GTK pointer acceptance requires system Python GI/GTK3")
        endif()
        add_test(NAME gtk_pointer_effects_test COMMAND gtk_pointer_effects_test
            "${MIRAGE_GTK_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/manual/gtk_pointer_fixture.py")
        set_tests_properties(gtk_pointer_effects_test PROPERTIES LABELS "integration;platform;manual" TIMEOUT 60)
        add_test(NAME gtk_pointer_scaled_test COMMAND gtk_pointer_effects_test
            "${MIRAGE_GTK_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/manual/gtk_pointer_fixture.py" --scaled)
        set_tests_properties(gtk_pointer_scaled_test PROPERTIES LABELS "integration;platform;manual" TIMEOUT 60)
        if(MIRAGE_X11_DRAG_HELPERS_AVAILABLE)
            add_test(NAME gtk_pointer_handshake_test COMMAND gtk_pointer_effects_test
                "${MIRAGE_GTK_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/manual/gtk_pointer_fixture.py" --handshake)
            set_tests_properties(gtk_pointer_handshake_test PROPERTIES LABELS "integration;platform;manual" TIMEOUT 60)
            add_test(NAME gtk_pointer_scaled_handshake_test COMMAND gtk_pointer_effects_test
                "${MIRAGE_GTK_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/manual/gtk_pointer_fixture.py" --scaled-handshake)
            set_tests_properties(gtk_pointer_scaled_handshake_test PROPERTIES LABELS "integration;platform;manual" TIMEOUT 60)
        endif()
    endif()
    add_executable(x11_backend_test platform/x11_backend_test.cpp)
    target_compile_features(x11_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(x11_backend_test)
    get_target_property(_mirage_x11_test_portal mirage_platform MIRAGE_PORTAL_AVAILABLE)
    if(_mirage_x11_test_portal)
        target_compile_definitions(x11_backend_test PRIVATE MIRAGE_TEST_PORTAL=1)
    endif()
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

# M5-09 X11 shape overlay carrier (DEC-029): exercised against a private
# headless Xvfb like the M2-02 backend tests. Registered only where the
# real overlay frontend is compiled (the stub build returns null and has
# nothing honest to exercise); the shape-extension condition mirrors
# platform/CMakeLists.txt.
if(X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND AND X11_Xshape_FOUND)
    add_executable(overlay_carrier_test platform/overlay_carrier_test.cpp)
    target_compile_features(overlay_carrier_test PUBLIC cxx_std_20)
    mirage_enable_warnings(overlay_carrier_test)
    target_link_libraries(overlay_carrier_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            ${X11_X11_LIB}
            ${X11_Xtst_LIB}
            ${X11_Xext_LIB}
    )
    add_test(NAME overlay_carrier_test COMMAND overlay_carrier_test)
    set_tests_properties(overlay_carrier_test PROPERTIES
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

    # M7-10 tiered screen capture engine (DEC-086): drives the DXGI Desktop
    # Duplication, Windows.Graphics.Capture and GDI BitBlt implementations
    # each on their own against the session's real desktop (same DEC-017
    # decision 9 discipline). Whitebox: it includes the engine's private src
    # header, because tier identity is an implementation choice the provider
    # contract deliberately hides. The test process declares DPI awareness so
    # the DEC-086 gate lets the GPU tiers through regardless of the host
    # display scale.
    add_executable(win32_capture_tier_test platform/win32_capture_tier_test.cpp)
    target_compile_features(win32_capture_tier_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_capture_tier_test)
    target_include_directories(win32_capture_tier_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/src
    )
    target_link_libraries(win32_capture_tier_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            user32
            gdi32
    )
    add_test(NAME win32_capture_tier_test COMMAND win32_capture_tier_test)
    # The four tier scenario sets each take up to ~2s of bounded platform
    # waits on a slow desktop; 120s leaves a wide margin like the sibling
    # win32 tests.
    set_tests_properties(win32_capture_tier_test PROPERTIES
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

    # M4-07 end-to-end closed loop on the real interactive desktop: the
    # observe -> action -> observe cycle through the ObservationAssembler,
    # the ElementTargetExecutor and MiraEnvironmentBinding against the real
    # Win32 + UIA frontends and a real fixture window (same DEC-017 decision
    # 9 discipline as the other win32 suites: runtime evidence needs a
    # desktop session; the MinGW cross build provides compile evidence).
    add_executable(win32_observation_e2e_test integration/win32_observation_e2e_test.cpp)
    target_compile_features(win32_observation_e2e_test PUBLIC cxx_std_20)
    mirage_enable_warnings(win32_observation_e2e_test)
    target_link_libraries(win32_observation_e2e_test
        PRIVATE
            Mirage::integration_mira
            Mirage::platform
            Mirage::desktop
    )
    add_test(NAME win32_observation_e2e_test COMMAND win32_observation_e2e_test)
    set_tests_properties(win32_observation_e2e_test PROPERTIES
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
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
    )
    target_link_libraries(visual_observation_e2e_test PRIVATE
        $<TARGET_FILE:kairo::kairo>
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
        # Private desktop entries and HTTP arguments need only local VFS.
        # Do not load the host's GVFS module/daemon into this isolated fixture.
        # The GIO libraries in the platform backend run internal worker
        # threads (same situation as the AT-SPI2 fixture); suppress exactly
        # the uninstrumented system glib races, keep Mirage frames reportable.
        ENVIRONMENT "GIO_USE_VFS=local;TSAN_OPTIONS=halt_on_error=1:suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
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

    # M5-10 Linux tray carrier (DEC-030) against a private D-Bus session and
    # a wire-exact fake SNI watcher host: capability honesty, the
    # specification conformance of the item registration, the menu Event →
    # action delivery, the wakeup-driven state push, the clean stop and the
    # watcher-vanished diagnostic exit (the notification test's fixture
    # topology).
    add_executable(tray_backend_test platform/tray_backend_test.cpp)
    target_compile_features(tray_backend_test PUBLIC cxx_std_20)
    mirage_enable_warnings(tray_backend_test)
    target_link_libraries(tray_backend_test
        PRIVATE
            Mirage::platform
            Mirage::desktop
            PkgConfig::GIO_TEST
    )
    add_test(NAME tray_backend_test COMMAND tray_backend_test)
    set_tests_properties(tray_backend_test PROPERTIES
        LABELS "integration"
        TIMEOUT 120
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
    )
endif()

# POSIX-only: unistd fixtures.
# 
if(UNIX)
add_executable(mira_binding_test integration/mira_binding_test.cpp)
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
# The M3-05 wired-binding scenarios run the concrete VisualObservationPipeline
# over the fake environment's screen provider; the pipeline drives the pinned
# Executor directly (the per-source blocking worker), consumed as SYSTEM
# headers and linked by file path (same form as the visual unit tests).
target_include_directories(mira_binding_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(mira_binding_test PRIVATE
    $<TARGET_FILE:kairo::kairo>
)
add_test(NAME mira_binding_test COMMAND mira_binding_test)
set_tests_properties(mira_binding_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
)
endif()

# M5-04 session conversation journal (DEC-021): platform-neutral library
# test over the pinned event store; the pinned include dirs propagate from
# Mirage::integration_mira by design (the adapter is the pinned-facing
# boundary).
add_executable(session_journal_test integration/session_journal_test.cpp)
target_compile_features(session_journal_test PUBLIC cxx_std_20)
mirage_enable_warnings(session_journal_test)
target_link_libraries(session_journal_test
    PRIVATE
        Mirage::integration_mira
)
add_test(NAME session_journal_test COMMAND session_journal_test)
set_tests_properties(session_journal_test PROPERTIES
    LABELS "unit;integration"
    TIMEOUT 60
)

# M5-05 workflow event bridge (DEC-023): platform-neutral library test over
# the pinned workflow event vocabulary; the pinned include dirs propagate
# from Mirage::integration_mira by design (the adapter is the pinned-facing
# boundary).
add_executable(workflow_event_bridge_test integration/workflow_event_bridge_test.cpp)
target_compile_features(workflow_event_bridge_test PUBLIC cxx_std_20)
mirage_enable_warnings(workflow_event_bridge_test)
target_link_libraries(workflow_event_bridge_test
    PRIVATE
        Mirage::integration_mira
)
add_test(NAME workflow_event_bridge_test COMMAND workflow_event_bridge_test)
set_tests_properties(workflow_event_bridge_test PROPERTIES
    LABELS "unit;integration"
    TIMEOUT 60
)

# M5-05 desktop atom toolset (DEC-024): registry population, permission
# gating and the ToolCall execution path over the fake desktop environment;
# the pinned include dirs propagate from Mirage::integration_mira by design.
add_executable(desktop_atom_toolset_test integration/desktop_atom_toolset_test.cpp)
target_compile_features(desktop_atom_toolset_test PUBLIC cxx_std_20)
mirage_enable_warnings(desktop_atom_toolset_test)
target_link_libraries(desktop_atom_toolset_test
    PRIVATE
        Mirage::integration_mira
)
add_test(NAME desktop_atom_toolset_test COMMAND desktop_atom_toolset_test)
add_executable(local_visual_workflow_test integration/local_visual_workflow_test.cpp)
target_include_directories(local_visual_workflow_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
target_compile_features(local_visual_workflow_test PRIVATE cxx_std_20)
mirage_enable_warnings(local_visual_workflow_test)
target_link_libraries(local_visual_workflow_test PRIVATE Mirage::integration_mira kairo)
add_test(NAME local_visual_workflow_test COMMAND local_visual_workflow_test)
set_tests_properties(local_visual_workflow_test PROPERTIES TIMEOUT 60 LABELS "integration;runtime;security")

add_executable(local_semantic_workflow_test integration/local_semantic_workflow_test.cpp)
target_include_directories(local_semantic_workflow_test SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
target_compile_features(local_semantic_workflow_test PRIVATE cxx_std_20)
mirage_enable_warnings(local_semantic_workflow_test)
target_link_libraries(local_semantic_workflow_test PRIVATE Mirage::integration_mira kairo)
add_test(NAME local_semantic_workflow_test COMMAND local_semantic_workflow_test)
set_tests_properties(local_semantic_workflow_test PROPERTIES
    LABELS "unit;integration;runtime" TIMEOUT 60)
set_tests_properties(desktop_atom_toolset_test PROPERTIES
    LABELS "unit;integration"
    TIMEOUT 60
)

# POSIX-only: signal self-pipe fixtures.
# 
if(UNIX)
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
endif()

# POSIX-only: exercises the Unix-socket transport and a forked helper.
#    # product-process round trip is win32_product_process_test.
if(UNIX)
add_executable(ipc_protocol_test runtime/ipc_protocol_test.cpp)
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

# POSIX-only: Unix-socket endpoints and a signal self-pipe; the Windows
# product-process round trip is win32_product_process_test.
# 
if(UNIX)
# M5-12 (DEC-032): the updater core — manifest decode, ed25519
# verification and the atomic apply engine. Registered only where OpenSSL
# libcrypto exists (the same optional-boundary discipline as the library
# itself: without it the crypto face fails closed and this test is
# meaningless).
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    add_executable(update_core_test runtime/update_core_test.cpp)
    target_compile_features(update_core_test PUBLIC cxx_std_20)
    mirage_enable_warnings(update_core_test)
    target_link_libraries(update_core_test
        PRIVATE
            Mirage::update
            OpenSSL::Crypto
    )
    add_test(NAME update_core_test COMMAND update_core_test)
    set_tests_properties(update_core_test PROPERTIES
        LABELS "unit"
        TIMEOUT 120
    )
else()
    message(STATUS "update_core_test disabled: OpenSSL not found")
endif()

# M5-12 update client end-to-end (independent verification, DEC-032): the
# real HTTP channel on 127.0.0.1 (in-process server) driving UpdateClient
# check/apply — byte-budget overrun, 3xx no-follow, non-200, same-size sha
# tampering with the target untouched, and the rollback invariant. The HTTP
# server and the fetcher under test are the POSIX port (the Windows fetcher
# port is compile-gated by the MinGW cross build); needs the crypto face.
if(UNIX AND OPENSSL_FOUND)
    add_executable(update_client_http_test runtime/update_client_http_test.cpp)
    target_compile_features(update_client_http_test PUBLIC cxx_std_20)
    mirage_enable_warnings(update_client_http_test)
    target_link_libraries(update_client_http_test
        PRIVATE
            Mirage::update
            OpenSSL::Crypto
    )
    add_test(NAME update_client_http_test COMMAND update_client_http_test)
    set_tests_properties(update_client_http_test PROPERTIES
        LABELS "unit"
        TIMEOUT 120
    )
endif()

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
        Mirage::persistence
        Mirage::permission
)
add_test(NAME runtime_service_test COMMAND runtime_service_test)
set_tests_properties(runtime_service_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)
endif()

# M5-06 (DEC-026): the observation face end to end over Local IPC — a real
# RuntimeService serves desktop.observe against the live Xvfb + AT-SPI fixture
# (semantic projection from a real accessibility tree; the visual component's
# fail-closed behaviour and its wired-registry success path). Same X11/AT-SPI
# precondition as observation_e2e_test.
if(UNIX AND ATSPI_TEST_FOUND AND X11_FOUND)
add_executable(observation_face_test runtime/observation_face_test.cpp)
target_compile_features(observation_face_test PUBLIC cxx_std_20)
mirage_enable_warnings(observation_face_test)
target_link_libraries(observation_face_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
        PkgConfig::ATSPI_TEST
        ${X11_X11_LIB}
)
add_test(NAME observation_face_test COMMAND observation_face_test)
set_tests_properties(observation_face_test PROPERTIES
    LABELS "integration"
    TIMEOUT 120
    ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp"
)
endif()

# POSIX-only: same Unix-socket transport coupling as runtime_service_test.
# 
if(UNIX)
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
endif()

# POSIX-only: same Unix-socket transport coupling as runtime_service_test
# (the M5-02 long-lived session client against a real RuntimeService; the
# Windows named-pipe session path is covered by win32_product_process_test).
if(UNIX)
add_executable(session_client_test runtime/session_client_test.cpp)
target_compile_features(session_client_test PUBLIC cxx_std_20)
mirage_enable_warnings(session_client_test)
target_link_libraries(session_client_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::ipc
)
add_test(NAME session_client_test COMMAND session_client_test)
# 600s: liveness guard, not a latency claim. The suite drives seven full
# service round trips (M5-05 added the workflow face scenario) and CI runs
# it under parallel ctest oversubscription (the M2-06 budget rationale).
set_tests_properties(session_client_test PROPERTIES
    LABELS "integration"
    TIMEOUT 600
)
endif()

# POSIX-only: the Linux filesystem-store hardening surface it exercises is
# a platform/linux header set (M1-05); the Windows store is covered by
# win32_product_process_test.
# 
if(UNIX)
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
endif()

add_executable(permission_test runtime/permission_test.cpp)
target_compile_features(permission_test PUBLIC cxx_std_20)
mirage_enable_warnings(permission_test)
target_link_libraries(permission_test PRIVATE Mirage::permission)
add_test(NAME permission_test COMMAND permission_test)
set_tests_properties(permission_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# M5-09 Desktop Overlay presenter (DEC-029): the service-internal
# presentation path against a scripted carrier — composition, latest-state
# dedup, budgets, the hub-probe confirmation revalidation and the
# stop/failure paths. The presenter is an internal module (runtime/service/
# src): the test reaches it through the source-tree include path, and the
# kairo headers enter as SYSTEM exactly like the service target consumes
# them; the kairo library links by file path so its interface include
# dirs do not leak.
add_executable(overlay_presenter_test runtime/overlay_presenter_test.cpp)
target_compile_features(overlay_presenter_test PUBLIC cxx_std_20)
mirage_enable_warnings(overlay_presenter_test)
target_include_directories(overlay_presenter_test PRIVATE
    ${CMAKE_SOURCE_DIR}/runtime/service/src
)
target_include_directories(overlay_presenter_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(overlay_presenter_test
    PRIVATE
        Mirage::runtime_service
        $<TARGET_FILE:kairo::kairo>
)
add_test(NAME overlay_presenter_test COMMAND overlay_presenter_test)
set_tests_properties(overlay_presenter_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)

# POSIX-only: asserts the 0700/0600 store hardening; the Windows store is
# covered by win32_product_process_test.
# # covered by win32_product_process_test.
if(UNIX)
add_executable(persistence_test runtime/persistence_test.cpp)
target_compile_features(persistence_test PUBLIC cxx_std_20)
mirage_enable_warnings(persistence_test)
target_link_libraries(persistence_test PRIVATE Mirage::persistence)
add_test(NAME persistence_test COMMAND persistence_test)
set_tests_properties(persistence_test PROPERTIES
    LABELS "unit"
    TIMEOUT 60
)
endif()

# POSIX-only: the ipc_io support header (poll-driven frame helpers) and the
# Linux desktop environment fixtures couple it to the Unix transport.
if(UNIX)
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
endif()

# POSIX-only: the ipc_io support header couples it to the Unix transport
# (poll.h); the permission matrix itself is platform-neutral and covered by
# permission_test on every platform.
if(UNIX)
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
endif()

# M5-03 permission async confirmation surface (DEC-020): the live-service
# scenarios ride the same Unix-socket transport coupling as
# task_permission_test; the Windows confirmation surface shares the hub unit
# tests (permission_test) and the win32 named-pipe product process test.
if(UNIX)
add_executable(permission_ipc_test runtime/permission_ipc_test.cpp)
target_compile_features(permission_ipc_test PUBLIC cxx_std_20)
mirage_enable_warnings(permission_ipc_test)
target_link_libraries(permission_ipc_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME permission_ipc_test COMMAND permission_ipc_test)
set_tests_properties(permission_ipc_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)
endif()

# M5-09 Desktop Overlay service integration (DEC-029): the overlay pump on
# the service's blocking worker, the confirmation mirror + confirm-entry
# click resolution, the IPC-first clearing discipline and the broken-carrier
# loud degradation — all over the Unix-socket transport like
# permission_ipc_test (the Windows product surface shares the presenter unit
# tests and the win32 overlay frontend's own CI job).
if(UNIX)
add_executable(overlay_service_test runtime/overlay_service_test.cpp)
target_compile_features(overlay_service_test PUBLIC cxx_std_20)
mirage_enable_warnings(overlay_service_test)
target_link_libraries(overlay_service_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME overlay_service_test COMMAND overlay_service_test)
set_tests_properties(overlay_service_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)
endif()

# POSIX-only: signal-driven cancellation probes.
# 
if(UNIX)
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
endif()

# M5-10 task pause/resume face (DEC-030): the pinned pause family over the
# live service — the parked driving loop, the no-mis-settlement invariant,
# the event advance and the cancel/guard matrix ride the same Unix-socket
# transport coupling as task_cancel_test; codec coverage lives in the golden
# vectors (v10) and ipc_protocol_test.
if(UNIX)
add_executable(task_pause_test runtime/task_pause_test.cpp)
target_compile_features(task_pause_test PUBLIC cxx_std_20)
mirage_enable_warnings(task_pause_test)
target_link_libraries(task_pause_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::platform
        Mirage::desktop
        Mirage::ipc
)
add_test(NAME task_pause_test COMMAND task_pause_test)
set_tests_properties(task_pause_test PROPERTIES
    LABELS "integration"
    TIMEOUT 300
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
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(visual_session_test PRIVATE
    $<TARGET_FILE:kairo::kairo>
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
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(visual_observation_loop_test PRIVATE
    $<TARGET_FILE:kairo::kairo>
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
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_link_libraries(visual_cache_geometry_test PRIVATE
    $<TARGET_FILE:kairo::kairo>
)
add_test(NAME visual_cache_geometry_test COMMAND visual_cache_geometry_test)
set_tests_properties(visual_cache_geometry_test PROPERTIES
    LABELS "unit"
    TIMEOUT 120
)

# M5-11 update channel (DEC-006 decision 5): the producer/verifier pair's
# fail-closed matrix — positive round trip, tampered/missing artifact,
# untrusted key, wrong schema and empty file list under VALID signatures,
# and the producer's refusal to emit an unsigned manifest. POSIX-only by
# nature (bash + openssl producer toolchain, the release-side Linux shape);
# the consumer side is the Windows updater (M5-12).
if(UNIX)
    find_program(UPDATE_TEST_BASH bash)
    find_program(UPDATE_TEST_OPENSSL openssl)
    if(UPDATE_TEST_BASH AND UPDATE_TEST_OPENSSL)
        add_test(NAME update_channel_test
            COMMAND ${UPDATE_TEST_BASH}
                ${CMAKE_CURRENT_SOURCE_DIR}/packaging/update_channel_test.sh)
        set_tests_properties(update_channel_test PROPERTIES
            LABELS "unit;smoke"
            TIMEOUT 120
        )
    endif()
endif()

# M5-01 lock-gate regression (DEC-006 decision 6): schema v2 structural
# checks, the frontend npm-tree hash gate and the mirage_require_locked_
# artifact() consumption gate, exercised positively and negatively in cmake
# script mode against the real dependencies.lock.json. No compiled binary;
# portable across the Linux matrix and the Windows job.
add_test(NAME dependency_lock_gate_test
    COMMAND ${CMAKE_COMMAND}
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/dependency_lock_gate_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_lock_test.cmake)
set_tests_properties(dependency_lock_gate_test PROPERTIES
    LABELS "unit;smoke"
    TIMEOUT 60
)

if(UNIX)
    add_executable(harness_desktop_tools_test runtime/harness_desktop_tools_test.cpp)
    target_include_directories(harness_desktop_tools_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    target_link_libraries(harness_desktop_tools_test PRIVATE Mirage::runtime_service
        Mirage::ipc Mirage::persistence Mirage::integration_mira kairo)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
        target_compile_definitions(harness_desktop_tools_test PRIVATE MIRAGE_HARNESS_X11)
        target_include_directories(harness_desktop_tools_test PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(harness_desktop_tools_test PRIVATE Mirage::platform ${X11_X11_LIB})
    endif()
    mirage_enable_warnings(harness_desktop_tools_test)
    add_test(NAME harness_desktop_tools_test COMMAND harness_desktop_tools_test)
    set_tests_properties(harness_desktop_tools_test PROPERTIES LABELS "integration;runtime" TIMEOUT 30)

    add_executable(native_agent_integration_test runtime/native_agent_integration_test.cpp
        ${CMAKE_SOURCE_DIR}/apps/native/runtime_bridge.cpp ${CMAKE_SOURCE_DIR}/apps/native/attachment.cpp)
    target_include_directories(native_agent_integration_test PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/native)
    target_include_directories(native_agent_integration_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    target_link_libraries(native_agent_integration_test PRIVATE Mirage::runtime_service
        Mirage::ipc Mirage::persistence Mirage::integration_mira kairo)
    mirage_enable_warnings(native_agent_integration_test)
    add_test(NAME native_agent_integration_test COMMAND native_agent_integration_test)
    # Includes escaped multi-MiB persistence/recovery; fully instrumented Debug
    # builds need a larger harness budget. Individual operation deadlines stay bounded.
    set_tests_properties(native_agent_integration_test PROPERTIES LABELS "integration;runtime;ui" TIMEOUT 120)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MIRAGE_ENABLE_NATIVE_FRONTEND)
    add_executable(native_conversation_view_test desktop/native_conversation_view_test.cpp
        ${CMAKE_SOURCE_DIR}/apps/native/chat_model.cpp
        ${CMAKE_SOURCE_DIR}/apps/native/window_controls.cpp
        ${CMAKE_SOURCE_DIR}/apps/native/runtime_bridge.cpp
        ${CMAKE_SOURCE_DIR}/apps/native/attachment.cpp)
    add_dependencies(native_conversation_view_test mirage-native)
    target_include_directories(native_conversation_view_test PRIVATE ${CMAKE_SOURCE_DIR}/apps/native)
    target_include_directories(native_conversation_view_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/eui-neo ${CMAKE_SOURCE_DIR}/third_party/eui-neo/include
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    target_link_libraries(native_conversation_view_test PRIVATE eui::neo Mirage::ipc Mirage::persistence kairo)
    mirage_enable_warnings(native_conversation_view_test)
    add_test(NAME native_conversation_view_test COMMAND native_conversation_view_test)
    set_tests_properties(native_conversation_view_test PROPERTIES LABELS "integration;ui" TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/apps/native")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(tray_runtime_test runtime/tray_runtime_test.cpp)
    target_compile_features(tray_runtime_test PRIVATE cxx_std_20)
    target_include_directories(tray_runtime_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    target_link_libraries(tray_runtime_test PRIVATE Mirage::runtime_service Mirage::integration_mira)
    mirage_enable_warnings(tray_runtime_test)
    add_test(NAME tray_runtime_test COMMAND tray_runtime_test)
    set_tests_properties(tray_runtime_test PROPERTIES LABELS "integration;runtime;lifecycle" TIMEOUT 30)

    # M8-01: pet status projection (status.hpp contract: task/dialog state -> PetStatus).
    add_executable(pet_status_test runtime/pet_status_test.cpp)
    target_compile_features(pet_status_test PRIVATE cxx_std_20)
    target_link_libraries(pet_status_test PRIVATE Mirage::pet_state Mirage::ipc)
    mirage_enable_warnings(pet_status_test)
    add_test(NAME pet_status_test COMMAND pet_status_test)
    set_tests_properties(pet_status_test PROPERTIES LABELS "unit;pet" TIMEOUT 10)

    # M8-02/M8-03: pet animation player (animation.hpp: frame sequencing, loop seams, transition timing).
    add_executable(pet_animation_test runtime/pet_animation_test.cpp)
    target_compile_features(pet_animation_test PRIVATE cxx_std_20)
    target_link_libraries(pet_animation_test PRIVATE Mirage::pet_state)
    mirage_enable_warnings(pet_animation_test)
    add_test(NAME pet_animation_test COMMAND pet_animation_test)
    set_tests_properties(pet_animation_test PROPERTIES LABELS "unit;pet" TIMEOUT 10)

    # POSIX fixture waits; the product adapter itself remains portable.
    add_executable(pet_projection_refresh_test runtime/pet_projection_refresh_test.cpp)
    target_compile_features(pet_projection_refresh_test PRIVATE cxx_std_20)
    target_include_directories(pet_projection_refresh_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    target_link_libraries(pet_projection_refresh_test PRIVATE kairo)
    mirage_enable_warnings(pet_projection_refresh_test)
    add_test(NAME pet_projection_refresh_test COMMAND pet_projection_refresh_test)
    set_tests_properties(pet_projection_refresh_test PROPERTIES LABELS "unit;pet;lifecycle" TIMEOUT 15)

    # M8-01: end-to-end status projection (runtime_service dialog settlement -> IPC event -> pet status).
    add_executable(pet_status_integration_test runtime/pet_status_integration_test.cpp)
    target_compile_features(pet_status_integration_test PRIVATE cxx_std_20)
    target_include_directories(pet_status_integration_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    target_link_libraries(pet_status_integration_test PRIVATE
        Mirage::runtime_service Mirage::integration_mira Mirage::pet_state)
    mirage_enable_warnings(pet_status_integration_test)
    add_test(NAME pet_status_integration_test COMMAND pet_status_integration_test)
    set_tests_properties(pet_status_integration_test PROPERTIES LABELS "integration;runtime;pet" TIMEOUT 30)
endif()

add_executable(action_contract_test desktop/action_contract_test.cpp)
target_compile_definitions(action_contract_test PRIVATE
    MIRAGE_ACTION_SCHEMA_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/desktop/data/m7_desktop_tools_schema.json")
target_include_directories(action_contract_test SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
target_link_libraries(action_contract_test PRIVATE Mirage::desktop Mirage::integration_mira kairo)
mirage_enable_warnings(action_contract_test)
add_test(NAME action_contract_test COMMAND action_contract_test)
set_tests_properties(action_contract_test PROPERTIES LABELS "integration;runtime" TIMEOUT 30)

# POSIX-coupled: the pretimeout scenario sleeps through <poll.h>; see the
# ppocr_pipeline_test note above.
if(UNIX)
    add_executable(desktop_image_supply_test integration/desktop_image_supply_test.cpp)
    target_include_directories(desktop_image_supply_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    target_link_libraries(desktop_image_supply_test PRIVATE Mirage::integration_mira Mirage::persistence kairo)
    mirage_enable_warnings(desktop_image_supply_test)
    add_test(NAME desktop_image_supply_test COMMAND desktop_image_supply_test)
    set_tests_properties(desktop_image_supply_test PROPERTIES LABELS "integration;runtime" TIMEOUT 30)

    # DEC-088 capability 2/3: DesktopContextBuilder compaction mapping and its
    # ConversationLoop seam wiring over the scripted-provider harness path.
    add_executable(desktop_context_builder_test integration/desktop_context_builder_test.cpp)
    target_include_directories(desktop_context_builder_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    target_link_libraries(desktop_context_builder_test PRIVATE Mirage::integration_mira kairo)
    mirage_enable_warnings(desktop_context_builder_test)
    add_test(NAME desktop_context_builder_test COMMAND desktop_context_builder_test)
    set_tests_properties(desktop_context_builder_test PROPERTIES LABELS "integration;runtime" TIMEOUT 120)
endif()

# M7-17: independent PNG round trip. The Python decoder re-runs the
# desktop_image_supply_test binary in a private directory and verifies every
# exported fixture (own CRC/Adler, own fixed-Huffman inflate, own unfilter)
# against the raw RGB companions — no production encoder logic involved.
find_program(MIRAGE_PNG_CHECK_PYTHON NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)
if(UNIX AND MIRAGE_PNG_CHECK_PYTHON)
    add_test(NAME desktop_png_decode_check COMMAND ${MIRAGE_PNG_CHECK_PYTHON}
        "${CMAKE_CURRENT_SOURCE_DIR}/manual/desktop_png_decode_check.py"
        "$<TARGET_FILE:desktop_image_supply_test>")
    set_tests_properties(desktop_png_decode_check PROPERTIES LABELS "integration;runtime" TIMEOUT 300)
endif()

add_executable(post_action_verification_test integration/post_action_verification_test.cpp)
target_include_directories(post_action_verification_test SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
target_link_libraries(post_action_verification_test PRIVATE Mirage::integration_mira kairo)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
    target_compile_definitions(post_action_verification_test PRIVATE MIRAGE_POST_ACTION_X11)
    target_include_directories(post_action_verification_test PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(post_action_verification_test PRIVATE Mirage::platform ${X11_X11_LIB})
endif()
mirage_enable_warnings(post_action_verification_test)
add_test(NAME post_action_verification_test COMMAND post_action_verification_test)
set_tests_properties(post_action_verification_test PROPERTIES LABELS "integration;runtime;platform" TIMEOUT 30)

add_executable(visual_text_verification_test integration/visual_text_verification_test.cpp)
target_include_directories(visual_text_verification_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
target_link_libraries(visual_text_verification_test PRIVATE Mirage::integration_mira kairo)
mirage_enable_warnings(visual_text_verification_test)
add_test(NAME visual_text_verification_test COMMAND visual_text_verification_test)
set_tests_properties(visual_text_verification_test PROPERTIES LABELS "integration;runtime;security" TIMEOUT 30)

add_executable(visual_marks_test integration/visual_marks_test.cpp)
target_include_directories(visual_marks_test SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
target_link_libraries(visual_marks_test PRIVATE Mirage::integration_mirador mirador::fusion kairo)
mirage_enable_warnings(visual_marks_test)
add_test(NAME visual_marks_test COMMAND visual_marks_test)
set_tests_properties(visual_marks_test PROPERTIES LABELS "integration;runtime;security" TIMEOUT 30)

# M7-16 / DEC-083: the target-region stability guard shared by the Wayland
# shared-input and X11 numbered-pointer re-verification, as a header-only
# unit over synthetic BGRA8 frames. The pinned mirador detection symbols are
# consumed through the static Mirage::integration_mirador archive; mirador::core
# re-exports the pinned include dirs (same shape as fake_visual_backend_test).
add_executable(desktop_stability_guard_test integration/desktop_stability_guard_test.cpp)
target_compile_features(desktop_stability_guard_test PUBLIC cxx_std_20)
mirage_enable_warnings(desktop_stability_guard_test)
target_link_libraries(desktop_stability_guard_test
    PRIVATE
        Mirage::desktop
        Mirage::integration_mirador
        mirador::core
)
add_test(NAME desktop_stability_guard_test COMMAND desktop_stability_guard_test)
set_tests_properties(desktop_stability_guard_test PROPERTIES LABELS "unit;integration" TIMEOUT 60)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
    add_executable(x11_visual_marks_test platform/x11_visual_marks_test.cpp)
    target_include_directories(x11_visual_marks_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
    target_link_libraries(x11_visual_marks_test PRIVATE Mirage::platform
        Mirage::integration_mirador mirador::fusion kairo ${X11_X11_LIB})
    mirage_enable_warnings(x11_visual_marks_test)
    add_test(NAME x11_visual_marks_test COMMAND x11_visual_marks_test)
    set_tests_properties(x11_visual_marks_test PROPERTIES LABELS "integration;platform" TIMEOUT 30)
endif()

if(MIRAGE_BUNDLE_VISION)
    add_test(NAME bundled_vision_resources_test COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/bundled_vision_resources_test.py"
        "${_vision_source}" "${CMAKE_SOURCE_DIR}" "${MIRAGE_VISION_BUNDLE_DIR}")
    set_tests_properties(bundled_vision_resources_test PROPERTIES LABELS "integration;supply-chain" TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_target_property(_mirage_portal_available mirage_platform MIRAGE_PORTAL_AVAILABLE)
    if(_mirage_portal_available)
        add_executable(portal_screen_test platform/portal_screen_test.cpp)
        target_compile_features(portal_screen_test PRIVATE cxx_std_20)
        mirage_enable_warnings(portal_screen_test)
        target_link_libraries(portal_screen_test PRIVATE Mirage::platform Mirage::integration_mira Mirage::integration_mirador kairo)
        target_include_directories(portal_screen_test SYSTEM PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
        get_target_property(_mirage_pipewire_available mirage_platform MIRAGE_PIPEWIRE_AVAILABLE)
        if(_mirage_pipewire_available)
            target_compile_definitions(portal_screen_test PRIVATE MIRAGE_TEST_PIPEWIRE=1)
            set(_mirage_portal_fixture_args --screencast)
        endif()
        add_test(NAME portal_screen_fixture COMMAND /usr/bin/python3
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/portal_screen_fixture.py $<TARGET_FILE:portal_screen_test> ${_mirage_portal_fixture_args})
        set_tests_properties(portal_screen_fixture PROPERTIES LABELS "integration;platform;wayland" TIMEOUT 120
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp")
        # Fifty isolated processes include two intentional 30-second request
        # deadlines. TSAN adds connection/teardown overhead even without load.
        # Extend only the aggregate harness budget; operation limits stay fixed.
        if(CMAKE_CXX_FLAGS MATCHES "fsanitize=thread")
            set_tests_properties(portal_screen_fixture PROPERTIES TIMEOUT 180)
        endif()
    endif()
endif()

add_executable(gui_fallback_test integration/gui_fallback_test.cpp)
target_include_directories(gui_fallback_test SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
target_link_libraries(gui_fallback_test PRIVATE Mirage::integration_mira kairo)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
    target_compile_definitions(gui_fallback_test PRIVATE MIRAGE_GUI_X11)
    target_include_directories(gui_fallback_test PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(gui_fallback_test PRIVATE Mirage::platform ${X11_X11_LIB})
endif()
mirage_enable_warnings(gui_fallback_test)
add_test(NAME gui_fallback_test COMMAND gui_fallback_test)
set_tests_properties(gui_fallback_test PROPERTIES LABELS "integration;runtime;platform;security" TIMEOUT 40)

# DEC-071: fixture owns a private bus and a synthetic Secret Service across processes.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND GIO_UNIX_FOUND)
    find_program(MIRAGE_GI_PYTHON NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)
    if(MIRAGE_GI_PYTHON)
        add_test(NAME credential_store_fixture_test COMMAND "${MIRAGE_GI_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/credential_store_fixture.py"
            "$<TARGET_FILE:credential_store_test>")
        set_tests_properties(credential_store_fixture_test PROPERTIES
            LABELS "integration;platform" TIMEOUT 90
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:suppressions=${CMAKE_SOURCE_DIR}/tests/support/tsan-glib.supp")
    endif()
endif()

# DEC-070: legacy Runtime fixtures may leave a persistence directory implicit.
# Give each test its own XDG stores, reset/cleaned by CTest fixtures. Separate
# build directories own separate stores; sanitizer environments are preserved.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_directory_property(_mirage_isolated_tests TESTS)
    set(_mirage_test_state "${CMAKE_CURRENT_BINARY_DIR}/isolated-state")
    add_test(NAME mirage_test_state_setup COMMAND ${CMAKE_COMMAND} -E rm -rf "${_mirage_test_state}")
    add_test(NAME mirage_test_state_cleanup COMMAND ${CMAKE_COMMAND} -E rm -rf "${_mirage_test_state}")
    set_tests_properties(mirage_test_state_setup PROPERTIES
        FIXTURES_SETUP mirage_isolated_state LABELS "unit;test-isolation" TIMEOUT 10)
    set_tests_properties(mirage_test_state_cleanup PROPERTIES
        FIXTURES_CLEANUP mirage_isolated_state LABELS "unit;test-isolation" TIMEOUT 10)
    foreach(_mirage_test IN LISTS _mirage_isolated_tests)
        set_property(TEST "${_mirage_test}" APPEND PROPERTY ENVIRONMENT
            "XDG_STATE_HOME=${_mirage_test_state}/${_mirage_test}/state"
            "XDG_CONFIG_HOME=${_mirage_test_state}/${_mirage_test}/config"
            "XDG_DATA_HOME=${_mirage_test_state}/${_mirage_test}/data")
        # Private Xvfb fixtures represent Xorg, even when the runner's real
        # desktop is Wayland. Portal fixtures override this in their child env.
        set_property(TEST "${_mirage_test}" APPEND PROPERTY ENVIRONMENT "XDG_SESSION_TYPE=x11")
        set_property(TEST "${_mirage_test}" APPEND PROPERTY FIXTURES_REQUIRED mirage_isolated_state)
    endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(portal_input_test platform/portal_input_test.cpp)
    target_link_libraries(portal_input_test PRIVATE Mirage::platform kairo)
    target_include_directories(portal_input_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    mirage_enable_warnings(portal_input_test)
    add_test(NAME portal_input_test COMMAND portal_input_test)
    set_tests_properties(portal_input_test PROPERTIES LABELS "platform;wayland" TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _mirage_portal_available)
    find_package(PNG REQUIRED)
    add_executable(wayland_input_host_test platform/wayland_input_host_test.cpp)
    target_link_libraries(wayland_input_host_test PRIVATE Mirage::platform Mirage::integration_mira kairo PNG::PNG)
    target_include_directories(wayland_input_host_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include)
    mirage_enable_warnings(wayland_input_host_test)
    # Manual system consent and a public native Wayland fixture required; no CTest.
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(portal_authorization_test platform/portal_authorization_test.cpp)
    target_link_libraries(portal_authorization_test PRIVATE Mirage::platform)
    mirage_enable_warnings(portal_authorization_test)
    add_test(NAME portal_authorization_test COMMAND portal_authorization_test)
    set_tests_properties(portal_authorization_test PROPERTIES LABELS "platform;wayland;security" TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _mirage_pipewire_available)
    add_executable(portal_stream_retention_test platform/portal_stream_retention_test.cpp)
    target_link_libraries(portal_stream_retention_test PRIVATE Mirage::desktop)
    if(TARGET PkgConfig::PIPEWIRE)
        target_link_libraries(portal_stream_retention_test PRIVATE PkgConfig::PIPEWIRE)
    else()
        target_include_directories(portal_stream_retention_test SYSTEM PRIVATE
            ${MIRAGE_PIPEWIRE_HEADERS} ${MIRAGE_SPA_HEADERS})
        target_link_libraries(portal_stream_retention_test PRIVATE ${MIRAGE_PIPEWIRE_LIBRARY})
    endif()
    target_link_options(portal_stream_retention_test PRIVATE
        -Wl,--wrap=pw_stream_dequeue_buffer -Wl,--wrap=pw_stream_queue_buffer
        -Wl,--wrap=pw_stream_destroy)
    mirage_enable_warnings(portal_stream_retention_test)
    add_test(NAME portal_stream_retention_test COMMAND portal_stream_retention_test)
    set_tests_properties(portal_stream_retention_test PROPERTIES LABELS "platform;wayland;unit" TIMEOUT 30)
endif()

add_executable(pet_bubble_test runtime/pet_bubble_test.cpp)
target_compile_features(pet_bubble_test PRIVATE cxx_std_20)
target_link_libraries(pet_bubble_test PRIVATE Mirage::pet_state Mirage::ipc)
mirage_enable_warnings(pet_bubble_test)
add_test(NAME pet_bubble_test COMMAND pet_bubble_test)
set_tests_properties(pet_bubble_test PROPERTIES LABELS "unit;runtime;pet;protocol" TIMEOUT 10)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MIRAGE_ENABLE_PET_PREVIEW)
    add_executable(pet_bubble_view_test desktop/pet_bubble_view_test.cpp)
    add_dependencies(pet_bubble_view_test mira_pet)
    target_include_directories(pet_bubble_view_test SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/eui-neo ${CMAKE_SOURCE_DIR}/third_party/eui-neo/include)
    target_include_directories(pet_bubble_view_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/pet/include)
    target_link_libraries(pet_bubble_view_test PRIVATE eui::neo mirage_pet_frames Mirage::persistence Mirage::ipc PNG::PNG)
    mirage_enable_warnings(pet_bubble_view_test)
    add_test(NAME pet_bubble_view_test COMMAND pet_bubble_view_test)
    set_tests_properties(pet_bubble_view_test PROPERTIES LABELS "integration;ui;pet" TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/apps/pet")
endif()
