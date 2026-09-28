# Contract-facing adapter between Mirage and the hosted Mira instance.
# The concrete binding lives here because it is the one layer allowed to
# include the pinned Mira headers (DEC-003); its public surface translates
# between the pinned environment contract and mirage::desktop (DEC-008).
add_library(mirage_integration_mira STATIC
    src/desktop_atom_toolset.cpp
    src/mira_environment_binding.cpp
    src/model_layer.cpp
    src/session_journal.cpp
    src/workflow_event_bridge.cpp
    # The model layer's credential resolver reads the environment per
    # platform (MSVC raises the CRT getenv as a warnings-as-errors
    # deprecation; DEC-017 / M4-06 toolchain fact).
    $<$<PLATFORM_ID:Windows>:src/model_layer_env_windows.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/model_layer_env_posix.cpp>
)
add_library(Mirage::integration_mira ALIAS mirage_integration_mira)

target_include_directories(mirage_integration_mira
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_integration_mira PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_integration_mira)

# The pinned socket transport's public header includes the pinned executor
# fabric directly (model_layer.cpp consumes it); the headers are consumed as
# system headers so the pinned library's own warning hygiene stays out of
# Mirage's -Werror build (same consumption form as mira_core / the service).
target_include_directories(mirage_integration_mira SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)

# The concrete binding header implements the pinned environment contract, so
# pinned include dirs propagate to consumers of this target by design; the
# desktop environment types are part of the adapter's public surface too.
# Mira::workflow backs the event bridge (DEC-023) and propagates the pinned
# workflow contract headers the same way.
target_link_libraries(mirage_integration_mira
    PUBLIC
        Mirage::desktop
        Mira::core
        Mira::workflow
    PRIVATE
        # DEC-027 model layer: the pinned portable socket HTTP/SSE transport
        # (POSIX + Winsock). https endpoints fail closed unless a TLS channel
        # adapter was built by the pinned dependency (OpenSSL adapter on UNIX
        # where its configure found OpenSSL); the layer never downgrades.
        Mira::net_transport
)

if(TARGET Mira::openssl_transport)
    target_link_libraries(mirage_integration_mira PRIVATE Mira::openssl_transport)
    target_compile_definitions(mirage_integration_mira PRIVATE MIRAGE_HAVE_OPENSSL_TLS)
endif()
