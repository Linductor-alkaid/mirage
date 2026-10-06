add_executable(mirage
    cli/main.cpp
    cli/update_command.cpp
)

target_compile_features(mirage PUBLIC cxx_std_20)
target_compile_definitions(mirage PRIVATE MIRAGE_VERSION="${PROJECT_VERSION}")
mirage_enable_warnings(mirage)

target_link_libraries(mirage
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::integration_mirador
        Mirage::platform
        Mirage::update
)

# Shared embedding keeps credentials/settings/platform bindings identical for
# the headless developer service and the product's tray-owned Runtime.
add_library(mirage_service_host STATIC service/host.cpp)
target_compile_features(mirage_service_host PUBLIC cxx_std_20)
target_compile_definitions(mirage_service_host PRIVATE MIRAGE_VERSION="${PROJECT_VERSION}")
mirage_enable_warnings(mirage_service_host)
target_link_libraries(mirage_service_host PRIVATE Mirage::runtime_service
    Mirage::integration_mira Mirage::persistence Mirage::platform)
add_executable(mirage-service service/main.cpp)
target_link_libraries(mirage-service PRIVATE mirage_service_host)
mirage_enable_warnings(mirage-service)

# DEC-045: the product tray embeds Runtime; missing carriers refuse startup.
add_subdirectory(tray)

# Native UI, DEC-037: the sole product frontend.
add_subdirectory(native)
