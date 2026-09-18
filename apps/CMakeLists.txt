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

# Development-time bridge (M1.5-03, DEC-012 decision 6): forwards Local IPC
# length-prefixed frames between browser WebSocket sessions and a real
# mirage-service Unix socket. It is a development tool only: no install
# rules, no product package, no new process-form commitment. Its internals
# drive the pinned Executor fabric directly, so executor headers are
# consumed as SYSTEM headers and the library is linked by file path (same
# consumption form as the runtime service).
add_library(mirage_devbridge_core STATIC
    devbridge/bridge_loop.cpp
    devbridge/websocket.cpp
)
target_include_directories(mirage_devbridge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/devbridge>
)
target_include_directories(mirage_devbridge_core SYSTEM PUBLIC
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_compile_features(mirage_devbridge_core PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_devbridge_core)
target_link_libraries(mirage_devbridge_core
    PUBLIC
        Mirage::ipc
    PRIVATE
        $<TARGET_FILE:executor::executor>
)

add_executable(mirage-devbridge
    devbridge/main.cpp
)

target_compile_features(mirage-devbridge PUBLIC cxx_std_20)
target_compile_definitions(mirage-devbridge PRIVATE MIRAGE_VERSION="${PROJECT_VERSION}")
mirage_enable_warnings(mirage-devbridge)

target_link_libraries(mirage-devbridge
    PRIVATE
        mirage_devbridge_core
)

# apps/desktop and apps/tray join with the M5 desktop product milestone.
