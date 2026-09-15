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

# Pinned Mira core, consumed in-tree from third_party/mira (DEC-001).
target_link_libraries(mirage_mira_host PUBLIC Mira::core)
