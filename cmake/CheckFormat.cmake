# Format check for Mirage's own sources. Gated on the mirage-format-check
# custom target so dependency builds are never touched.
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-19 clang-format-18 clang-format-17)
if(NOT CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR "clang-format was not found")
endif()

file(GLOB_RECURSE MIRAGE_FORMAT_FILES
    "${ROOT_DIR}/desktop/*/include/*.hpp"
    "${ROOT_DIR}/desktop/*/src/*.cpp"
    "${ROOT_DIR}/runtime/*/include/*.hpp"
    "${ROOT_DIR}/runtime/*/src/*.cpp"
    "${ROOT_DIR}/integration/*/include/*.hpp"
    "${ROOT_DIR}/integration/*/src/*.cpp"
    "${ROOT_DIR}/platform/include/*.hpp"
    "${ROOT_DIR}/platform/*/include/*.hpp"
    "${ROOT_DIR}/platform/*/src/*.cpp"
    "${ROOT_DIR}/apps/*/*.cpp"
    "${ROOT_DIR}/tests/*.cpp"
)
foreach(FILE_PATH IN LISTS MIRAGE_FORMAT_FILES)
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror "${FILE_PATH}"
        RESULT_VARIABLE FORMAT_RESULT
    )
    if(NOT FORMAT_RESULT EQUAL 0)
        message(FATAL_ERROR "Formatting check failed: ${FILE_PATH}")
    endif()
endforeach()
