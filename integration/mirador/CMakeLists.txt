add_library(mirage_integration_mirador STATIC
    src/fake_visual_backend.cpp
    src/mirador_service.cpp
)
add_library(Mirage::integration_mirador ALIAS mirage_integration_mirador)

target_include_directories(mirage_integration_mirador
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_integration_mirador PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_integration_mirador)

# Pinned mirador core, consumed in-tree from third_party/mirador (DEC-001).
# PRIVATE: pinned headers must not leak through Mirage's public interface.
target_link_libraries(mirage_integration_mirador PRIVATE mirador::core)
