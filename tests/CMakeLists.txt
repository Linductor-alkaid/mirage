# Test binaries follow the pinned mira house style: one plain main() per
# binary using tests/support/test.hpp; labels drive CI selection (docs/
# project/project-standards.md section 11).
add_executable(dependency_wiring_test smoke/dependency_wiring_test.cpp)
target_compile_features(dependency_wiring_test PUBLIC cxx_std_20)
mirage_enable_warnings(dependency_wiring_test)
target_link_libraries(dependency_wiring_test
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mirador
        Mirage::platform
)
add_test(NAME dependency_wiring_test COMMAND dependency_wiring_test)
set_tests_properties(dependency_wiring_test PROPERTIES
    LABELS "unit;smoke"
    TIMEOUT 60
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
