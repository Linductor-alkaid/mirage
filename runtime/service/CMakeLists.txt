add_library(mirage_runtime_service STATIC
    src/runtime_service.cpp
)
add_library(Mirage::runtime_service ALIAS mirage_runtime_service)

target_include_directories(mirage_runtime_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_runtime_service PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_runtime_service)

target_link_libraries(mirage_runtime_service
    PUBLIC
        Mirage::mira_host
        Mirage::desktop
)
