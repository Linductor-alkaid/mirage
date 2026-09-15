# Contract-facing adapter between Mirage and the hosted Mira instance.
# Header-only until the M1 binding introduces translated calls.
add_library(mirage_integration_mira INTERFACE)
add_library(Mirage::integration_mira ALIAS mirage_integration_mira)

target_include_directories(mirage_integration_mira
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_integration_mira INTERFACE cxx_std_20)
