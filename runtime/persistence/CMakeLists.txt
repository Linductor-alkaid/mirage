add_library(mirage_runtime_persistence STATIC
    src/recovery.cpp
    src/settings.cpp
    # The DEC-011 store and its default paths are platform-selected
    # implementations of one shared surface (M4-06): atomic save / capped
    # load on both sides, platform-native durability and paths.
    $<$<PLATFORM_ID:Windows>:src/store_windows.cpp>
    $<$<PLATFORM_ID:Windows>:src/paths_windows.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/store_posix.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/paths_posix.cpp>
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
