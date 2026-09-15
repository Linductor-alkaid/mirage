# Contract-facing adapter between Mirage and the hosted Mira instance.
# The concrete binding lives here because it is the one layer allowed to
# include the pinned Mira headers (DEC-003); its public surface translates
# between the pinned environment contract and mirage::desktop (DEC-008).
add_library(mirage_integration_mira STATIC
    src/mira_environment_binding.cpp
)
add_library(Mirage::integration_mira ALIAS mirage_integration_mira)

target_include_directories(mirage_integration_mira
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_integration_mira PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_integration_mira)

# The concrete binding header implements the pinned environment contract, so
# pinned include dirs propagate to consumers of this target by design; the
# desktop environment types are part of the adapter's public surface too.
target_link_libraries(mirage_integration_mira
    PUBLIC
        Mirage::desktop
        Mira::core
)
