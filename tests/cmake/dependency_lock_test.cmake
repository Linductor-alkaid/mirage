# Lock-gate regression test (M5-01, DEC-006 decision 6): drives
# cmake/MirageDependencies.cmake against the real repository lock (positive
# cases) and against synthesized malformed locks (negative cases), asserting
# the fail-closed behavior of the schema v2 checks, the frontend npm-tree hash
# gate and the mirage_require_locked_artifact() consumption gate.
#
# Script-mode cmake test — no compiled binary, no git/network access, portable
# across the Linux matrix and the Windows job. ctest registration lives in
# tests/CMakeLists.txt; direct invocation:
#
#   cmake -DREPO_ROOT=<repo root> -DBUILD_DIR=<scratch dir> \
#       -P tests/cmake/dependency_lock_test.cmake

if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR
        "dependency_lock_gate_test requires -DREPO_ROOT=<repo root>.")
endif()
if(NOT DEFINED CASE AND NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR
        "dependency_lock_gate_test requires -DBUILD_DIR=<scratch dir> in "
        "orchestrator mode.")
endif()
cmake_path(SET repo_root NORMALIZE "${REPO_ROOT}")

# ---------------------------------------------------------------------------
# Case runner mode: -DCASE=<name> -DFIXTURE_DIR=<dir> re-enters this script
# against one fixture. Negative cases end in an intentional FATAL_ERROR, so
# the orchestrator runs each case in a child cmake process.
# ---------------------------------------------------------------------------
if(DEFINED CASE)
    set(MIRAGE_DEPENDENCY_LOCK_DIR "${FIXTURE_DIR}")
    include("${repo_root}/cmake/MirageDependencies.cmake")
    if(CASE STREQUAL "require_registered")
        mirage_require_locked_artifact(cef cef_artifact)
        if(NOT cef_artifact_VERSION STREQUAL "152.0.8+g1ce985c+chromium-152.0.7977.134")
            message(FATAL_ERROR "unexpected cef_artifact_VERSION '${cef_artifact_VERSION}'")
        endif()
        if(NOT cef_artifact_URL MATCHES "https://cef-builds\\.spotifycdn\\.com/cef_binary_.+_{platform}\\.tar\\.bz2")
            message(FATAL_ERROR "unexpected cef_artifact_URL '${cef_artifact_URL}'")
        endif()
        if(NOT cef_artifact_UPSTREAM_INDEX STREQUAL "https://cef-builds.spotifycdn.com/index.json")
            message(FATAL_ERROR "unexpected cef_artifact_UPSTREAM_INDEX '${cef_artifact_UPSTREAM_INDEX}'")
        endif()
        message(STATUS "case require_registered: OK")
    elseif(CASE STREQUAL "schema_structure_ok")
        _mirage_dependency_lock(lock_json)
        mirage_require_lock_schema_v2("${lock_json}")
        mirage_validate_lock_extensions("${lock_json}")
        message(STATUS "case schema_structure_ok: OK")
    elseif(CASE STREQUAL "unregistered_artifact_rejected")
        mirage_require_locked_artifact(electron electron_artifact)
        message(FATAL_ERROR "expected configure failure did not occur")
    elseif(CASE STREQUAL "schema_v1_rejected")
        _mirage_dependency_lock(lock_json)
        mirage_require_lock_schema_v2("${lock_json}")
        message(FATAL_ERROR "expected configure failure did not occur")
    elseif(CASE STREQUAL "malformed_sha1_rejected"
            OR CASE STREQUAL "missing_member_rejected"
            OR CASE STREQUAL "npm_drift_rejected")
        _mirage_dependency_lock(lock_json)
        mirage_require_lock_schema_v2("${lock_json}")
        mirage_validate_lock_extensions("${lock_json}")
        message(FATAL_ERROR "expected configure failure did not occur")
    else()
        message(FATAL_ERROR "unknown CASE '${CASE}'")
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# Orchestrator mode.
# ---------------------------------------------------------------------------
set(fixture_dir "${BUILD_DIR}/fixture")
set(base_lock "${repo_root}/dependencies.lock.json")
set(base_lockfile "${repo_root}/ui/package-lock.json")
foreach(required base_lock base_lockfile)
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "dependency_lock_gate_test: missing ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${fixture_dir}")
file(MAKE_DIRECTORY "${fixture_dir}/ui")
file(COPY_FILE "${base_lock}" "${fixture_dir}/dependencies.lock.json")
file(COPY_FILE "${base_lockfile}" "${fixture_dir}/ui/package-lock.json")

# Restores the pristine fixture so each tampered case starts from the real,
# valid lock content.
function(_gate_reset fixture_dir base_lock base_lockfile)
    file(REMOVE_RECURSE "${fixture_dir}")
    file(MAKE_DIRECTORY "${fixture_dir}/ui")
    file(COPY_FILE "${base_lock}" "${fixture_dir}/dependencies.lock.json")
    file(COPY_FILE "${base_lockfile}" "${fixture_dir}/ui/package-lock.json")
endfunction()

function(_gate_case case fixture_dir repo_root build_dir expect_failure marker)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DREPO_ROOT=${repo_root}
            -DCASE=${case}
            -DFIXTURE_DIR=${fixture_dir}
            -P "${CMAKE_CURRENT_LIST_DIR}/dependency_lock_test.cmake"
        OUTPUT_VARIABLE case_stdout
        ERROR_VARIABLE case_stderr
        RESULT_VARIABLE case_result
    )
    if(expect_failure)
        if(case_result EQUAL 0)
            message(FATAL_ERROR
                "case ${case}: expected a configure failure but the case passed.")
        endif()
        # CMake message() wraps at column width; normalize whitespace runs so
        # the marker match is robust against line breaks.
        string(REGEX REPLACE "[ \t\r\n]+" " " case_output "${case_stdout}${case_stderr}")
        string(FIND "${case_output}" "${marker}" marker_at)
        if(marker_at EQUAL -1)
            message(FATAL_ERROR
                "case ${case}: failed as expected but the diagnostic is missing "
                "the marker '${marker}'. Output:\n${case_stdout}${case_stderr}")
        endif()
    else()
        if(NOT case_result EQUAL 0)
            message(FATAL_ERROR
                "case ${case}: expected success but the case failed. "
                "Output:\n${case_stdout}${case_stderr}")
        endif()
    endif()
    message(STATUS "dependency_lock_gate_test: case ${case} — OK")
endfunction()

# Positive: the real lock passes structural validation and the consumption
# gate resolves the registered cef artifact with its pinned coordinates.
_gate_case(require_registered "${fixture_dir}" "${repo_root}" "${BUILD_DIR}" FALSE "")
_gate_case(schema_structure_ok "${fixture_dir}" "${repo_root}" "${BUILD_DIR}" FALSE "")

# Negative: an unregistered artifact cannot be consumed (unlocked binaries do
# not enter the build).
_gate_case(unregistered_artifact_rejected "${fixture_dir}" "${repo_root}" "${BUILD_DIR}"
    TRUE "not registered in dependencies.lock.json")

# Negative: schema version rollback is rejected.
_gate_reset("${fixture_dir}" "${base_lock}" "${base_lockfile}")
file(READ "${fixture_dir}/dependencies.lock.json" tampered)
string(REPLACE "\"schema_version\": 2" "\"schema_version\": 1" tampered "${tampered}")
file(WRITE "${fixture_dir}/dependencies.lock.json" "${tampered}")
_gate_case(schema_v1_rejected "${fixture_dir}" "${repo_root}" "${BUILD_DIR}"
    TRUE "schema_version must be 2")

# Negative: a malformed digest fails the structural check instead of passing
# silently.
_gate_reset("${fixture_dir}" "${base_lock}" "${base_lockfile}")
file(READ "${fixture_dir}/dependencies.lock.json" tampered)
string(REPLACE "add0a51f7333bc660e8e3bafd998e0122568f7d8"
    "ADD0A51F7333BC660E8E3BAFD998E0122568F7D8" tampered "${tampered}")
file(WRITE "${fixture_dir}/dependencies.lock.json" "${tampered}")
_gate_case(malformed_sha1_rejected "${fixture_dir}" "${repo_root}" "${BUILD_DIR}"
    TRUE "40-hex")

# Negative: a missing required member fails closed.
_gate_reset("${fixture_dir}" "${base_lock}" "${base_lockfile}")
file(READ "${fixture_dir}/dependencies.lock.json" tampered)
string(REPLACE "\"url_template\":" "\"url_template_x\":" tampered "${tampered}")
file(WRITE "${fixture_dir}/dependencies.lock.json" "${tampered}")
_gate_case(missing_member_rejected "${fixture_dir}" "${repo_root}" "${BUILD_DIR}"
    TRUE "missing required member")

# Negative: npm lockfile drift (any change to ui/package-lock.json without
# updating frontend.lockfile_sha256) fails configure.
_gate_reset("${fixture_dir}" "${base_lock}" "${base_lockfile}")
file(APPEND "${fixture_dir}/ui/package-lock.json" "\n")
_gate_case(npm_drift_rejected "${fixture_dir}" "${repo_root}" "${BUILD_DIR}"
    TRUE "drifted from")

message(STATUS "dependency_lock_gate_test: all cases passed")
