add_library(mirage_cli STATIC
    cli/main.cpp
    cli/update_command.cpp
)

target_compile_features(mirage_cli PUBLIC cxx_std_20)
target_compile_definitions(mirage_cli PRIVATE MIRAGE_VERSION="${PROJECT_VERSION}")
mirage_enable_warnings(mirage_cli)

target_link_libraries(mirage_cli
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::integration_mirador
        Mirage::platform
        Mirage::update
        Mirage::persistence
)
add_executable(mirage cli/entry.cpp)
target_link_libraries(mirage PRIVATE mirage_cli)
mirage_enable_warnings(mirage)
if(WIN32)
    add_executable(mirage-launcher WIN32 cli/windows_launcher.cpp)
    target_link_libraries(mirage-launcher PRIVATE mirage_cli shell32 user32)
    if(MINGW)
        target_link_options(mirage-launcher PRIVATE -municode)
    endif()
    mirage_enable_warnings(mirage-launcher)
endif()

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

# Approved v5 sources live in apps/pet/assets. Preview/asset tests remain opt-in;
# product installation and redistribution are covered separately by M8-08.
option(MIRAGE_ENABLE_PET_PREVIEW "Build the local v5 mira_pet preview" OFF)
option(MIRAGE_ENABLE_PET_ASSET_TESTS "Test approved v5 assets without building a UI" OFF)
add_subdirectory(pet)

if(MIRAGE_BUNDLE_VISION)
    target_compile_definitions(mirage_service_host PRIVATE MIRAGE_BUNDLED_VISION=1)
endif()
