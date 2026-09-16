add_library(mirage_runtime_persistence STATIC
    src/paths.cpp
    src/recovery.cpp
    src/settings.cpp
    src/store_posix.cpp
)
add_library(Mirage::persistence ALIAS mirage_runtime_persistence)

target_include_directories(mirage_runtime_persistence
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_runtime_persistence PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_runtime_persistence)

# The pinned JSON model is an implementation detail hidden behind the
# pinned-free settings/recovery surfaces (DEC-011 item 2); public headers
# never leak it. Same consumption form as mirage_ipc (DEC-007 item 3).
target_link_libraries(mirage_runtime_persistence
    PRIVATE
        Mira::core
)
