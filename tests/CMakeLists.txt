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
