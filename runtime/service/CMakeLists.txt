add_library(mirage_runtime_service STATIC
    src/basic_output_store.cpp
    src/desktop_observer.cpp
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
        ${CMAKE_SOURCE_DIR}/integration/mirador/src
)
# Service internals drive the pinned Executor fabric directly through the
# shared consumer face (DEC-096): SYSTEM headers plus the file-path link
# below, so the pinned library's own warning hygiene and non-SYSTEM
# interface include dirs stay out of Mirage's -Werror build.
target_compile_features(mirage_runtime_service PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_runtime_service)
if(MSVC)
    # Runtime IPC variants and tracked dispatch templates exceed COFF's
    # default section count in Debug builds; retain the full warning gate.
    target_compile_options(mirage_runtime_service PRIVATE /bigobj)
endif()

# Public surface policy (DEC-096): RuntimeService/ServiceConfig name
# integration adapter types, so pinned headers arrive only through the
# integration boundary's two recorded transitive seams (BoundaryCheck
# allowlist) and never by direct include. The
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
        Mirage::integration_mirador
        Mirage::persistence
        Mirage::kairo_consumer
)
