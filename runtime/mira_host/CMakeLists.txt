add_library(mirage_mira_host STATIC
    src/mira_host.cpp
)
add_library(Mirage::mira_host ALIAS mirage_mira_host)

target_include_directories(mirage_mira_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_mira_host PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_mira_host)

# The public header names the mirage-free binding interface; the pinned Mira
# contract stays an implementation detail of the host (M1 header boundary).
target_link_libraries(mirage_mira_host
    PUBLIC
        Mirage::integration_mira
    PRIVATE
        Mira::core
)
