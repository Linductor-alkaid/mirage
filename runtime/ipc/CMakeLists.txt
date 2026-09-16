add_library(mirage_ipc STATIC
    src/client.cpp
    src/endpoint.cpp
    src/framing.cpp
    src/protocol.cpp
    src/stream_posix.cpp
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
