add_library(mirage_runtime_permission STATIC
    src/capability.cpp
    src/permission.cpp
)
add_library(Mirage::permission ALIAS mirage_runtime_permission)

target_include_directories(mirage_runtime_permission
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_runtime_permission PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_runtime_permission)
