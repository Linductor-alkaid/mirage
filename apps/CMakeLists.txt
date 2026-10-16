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
        Mirage::kairo_consumer
        Mirage::platform
        Mirage::update
        Mirage::persistence
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

# Approved v5 sources live in apps/pet/assets. Preview/asset tests remain opt-in;
# product installation and redistribution are covered separately by M8-08.
option(MIRAGE_ENABLE_PET_PREVIEW "Build the local v5 mira_pet preview" OFF)
option(MIRAGE_ENABLE_PET_ASSET_TESTS "Test approved v5 assets without building a UI" OFF)
add_subdirectory(pet)

# DEC-092: the documented product entry must build every tray-owned carrier,
# rather than accidentally relying on a pet left in an old build directory.
if(MIRAGE_ENABLE_NATIVE_FRONTEND AND TARGET mira_pet)
    add_custom_target(mirage-native-product
        DEPENDS mirage mirage-service mirage-tray mirage-native mira_pet)
endif()

if(MIRAGE_BUNDLE_VISION)
    target_compile_definitions(mirage_service_host PRIVATE MIRAGE_BUNDLED_VISION=1)
endif()
