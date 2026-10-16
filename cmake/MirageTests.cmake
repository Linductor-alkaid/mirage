# Shared test target constructor (REUSE-13). One plain main() per binary in
# the pinned mira house style: C++20, the house warning gate, explicit
# libraries, and CTest registration with labels (CI selection) and a timeout
# (hung-suite guard). Keywords:
#
#   mirage_add_test(<name>
#       SOURCES <src...>
#       [LIBS <libs...>]
#       [LABELS <labels...>]
#       TIMEOUT <seconds>
#       [DEFINES <defs...>]
#       [INCLUDE_DIRS <dirs...>]
#       [TEST_PROPERTIES <extra set_tests_properties pairs...>])
#
# Tests needing anything beyond this shape (fixtures, custom COMMAND,
# non-target tests) stay hand-written in tests/CMakeLists.txt.
function(mirage_add_test name)
    cmake_parse_arguments(MAT "" "TIMEOUT"
        "SOURCES;LIBS;LABELS;DEFINES;INCLUDE_DIRS;TEST_PROPERTIES" ${ARGN})
    if(NOT MAT_SOURCES)
        message(FATAL_ERROR "mirage_add_test(${name}): SOURCES is required")
    endif()
    add_executable(${name} ${MAT_SOURCES})
    target_compile_features(${name} PRIVATE cxx_std_20)
    mirage_enable_warnings(${name})
    if(MAT_DEFINES)
        target_compile_definitions(${name} PRIVATE ${MAT_DEFINES})
    endif()
    if(MAT_INCLUDE_DIRS)
        target_include_directories(${name} PRIVATE ${MAT_INCLUDE_DIRS})
    endif()
    if(MAT_LIBS)
        target_link_libraries(${name} PRIVATE ${MAT_LIBS})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(MAT_TEST_PROPERTIES)
        set_tests_properties(${name} PROPERTIES ${MAT_TEST_PROPERTIES})
    endif()
    if(MAT_LABELS)
        set_tests_properties(${name} PROPERTIES LABELS "${MAT_LABELS}")
    endif()
    if(MAT_TIMEOUT)
        set_tests_properties(${name} PROPERTIES TIMEOUT "${MAT_TIMEOUT}")
    endif()
endfunction()
