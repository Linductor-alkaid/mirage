add_library(mirage_ipc STATIC
    src/client.cpp
    src/framing.cpp
    src/protocol.cpp
    src/session_client.cpp
    # The DEC-007 transport is platform-selected: Unix domain sockets on
    # Linux, named pipes on Windows (M4-06). The frame format, the protocol
    # and the golden vectors are shared and unchanged.
    $<$<PLATFORM_ID:Windows>:src/stream_windows.cpp>
    $<$<PLATFORM_ID:Windows>:src/endpoint_windows.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/stream_posix.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/endpoint_posix.cpp>
)
add_library(Mirage::ipc ALIAS mirage_ipc)

target_include_directories(mirage_ipc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_ipc PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_ipc)

# The JSON wire codec is an implementation detail hidden behind the
# pinned-free protocol surface (DEC-007 item 3); public headers never leak
# the pinned JSON model.
target_link_libraries(mirage_ipc
    PRIVATE
        Mira::core
)

# The named pipe transport uses kernel base APIs only: no new import
# library beyond the pinned JSON codec consumption above.
