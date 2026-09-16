add_executable(mirage
    cli/main.cpp
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
)

# The background Runtime Service host (design doc section 12, DEC-007):
# GUI, Tray and CLI talk to it exclusively over Local IPC.
add_executable(mirage-service
    service/main.cpp
)

target_compile_features(mirage-service PUBLIC cxx_std_20)
target_compile_definitions(mirage-service PRIVATE MIRAGE_VERSION="${PROJECT_VERSION}")
mirage_enable_warnings(mirage-service)

target_link_libraries(mirage-service
    PRIVATE
        Mirage::runtime_service
        Mirage::integration_mira
        Mirage::persistence
        Mirage::platform
)

# apps/desktop and apps/tray join with the M5 desktop product milestone.
