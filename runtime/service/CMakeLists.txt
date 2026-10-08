add_library(mirage_runtime_service STATIC
    src/overlay_presenter.cpp
    src/recovery_writer.cpp
    src/runtime_service.cpp
    src/service_loop.cpp
    src/task_driver.cpp
    src/tray_presenter.cpp
)
add_library(Mirage::runtime_service ALIAS mirage_runtime_service)

target_include_directories(mirage_runtime_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Service internals drive the pinned Executor fabric directly; the headers
# are consumed as system headers so the pinned library's own warning hygiene
# stays out of Mirage's -Werror build (same consumption form as mira_core).
target_include_directories(mirage_runtime_service SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include"
)
target_compile_features(mirage_runtime_service PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_runtime_service)

# Public surface (RuntimeService, ServiceConfig) is pinned-free; the
# kairo fabric, the JSON codec and the pinned conversation journal are
# implementation details of the service internals (DEC-007, DEC-021). The
# kairo library is linked by file path so its non-SYSTEM interface include
# dirs do not leak into this target.
target_link_libraries(mirage_runtime_service
    PUBLIC
        Mirage::mira_host
        Mirage::ipc
        Mirage::permission
    PRIVATE
        Mirage::integration_mira
        Mirage::persistence
        $<TARGET_FILE:kairo::kairo>
)
