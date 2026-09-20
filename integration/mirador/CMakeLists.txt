add_library(mirage_integration_mirador STATIC
    src/fake_visual_backend.cpp
    src/mirador_service.cpp
    src/visual_backend_registry.cpp
    src/visual_execution_context.cpp
    src/visual_frame.cpp
    src/visual_observation_mapper.cpp
    src/visual_session_host.cpp
    src/visual_template_index.cpp
)
add_library(Mirage::integration_mirador ALIAS mirage_integration_mirador)

target_include_directories(mirage_integration_mirador
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_integration_mirador PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_integration_mirador)

# Pinned mirador session pipeline, consumed in-tree from third_party/mirador
# (DEC-001). Per the M3-02/M3-04 consumption face the integration links the
# highest modules it uses: mirador::fusion carries the PerceptionSession and
# PUBLICly pulls mirador::image (frame conversion kernels) and mirador::cache
# (frame / capability caches, visual index); M3-04 adds mirador::geometry for
# the line-detection SPI and the closed-region proposals. PRIVATE: pinned
# headers must not leak through Mirage's public interface.
target_link_libraries(mirage_integration_mirador
    PUBLIC
        Mirage::desktop
    PRIVATE
        mirador::fusion
        mirador::geometry
)

# The M3-02 session adapter drives the pinned Executor fabric directly (the
# per-source blocking worker, DEC-016 decision 4); headers are consumed as
# SYSTEM headers so the pinned library's own warning hygiene stays out of
# Mirage's -Werror build, and the library is linked by file path so its
# non-SYSTEM interface include dirs do not leak into this target (same
# consumption form as the runtime service).
target_include_directories(mirage_integration_mirador SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
target_link_libraries(mirage_integration_mirador PRIVATE
    $<TARGET_FILE:executor::executor>
)
