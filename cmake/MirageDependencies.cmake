# Resolve and verify the pins declared in dependencies.lock.json
# (docs/project/project-standards.md section 9.1; schema v2 since M5-01,
# DEC-006 decision 6).
#
# Lock file layout:
#   schema_version   must be 2.
#   dependencies[]   git submodules. Top-level entries are verified against
#                    the submodule worktree; nested_pins entries are verified
#                    against the gitlink recorded by their parent repository,
#                    so a parent checkout that silently moved a nested
#                    submodule fails configure.
#   artifacts[]      non-submodule binary pins (version + url + sha1 + size +
#                    license). Validated structurally at configure; the digest
#                    is enforced when the artifact is acquired (download and
#                    hash check, then unpack).
#   frontend         npm dependency tree registration. The declared lockfile
#                    sha256 is re-computed at every configure, so
#                    ui/package-lock.json drift without updating this lock file
#                    fails the build. The digest is defined over the
#                    LF-normalized file content (identical to the committed
#                    blob), so the gate is invariant to checkout line-ending
#                    conversion (Windows autocrlf working trees vs LF checkouts).
#
# Two operating modes:
#   MIRAGE_FETCH_DEPENDENCIES=ON  (default) sync missing submodules at
#                                 configure time, then verify commits.
#   MIRAGE_FETCH_DEPENDENCIES=OFF verify-only path for offline / CI builds;
#                                 a missing checkout is a hard error.
#
# Structural checks and the frontend hash gate run unconditionally: they are
# local, cheap, and independent of git/network availability. Submodule commit
# verification stays behind MIRAGE_VERIFY_DEPENDENCIES.
#
# Gate (DEC-006 decision 6): no unlocked binary may enter the default build.
# The default build graph contains no consumer of any pinned artifact; a
# product target that starts consuming one (the M5-02 apps/desktop shell is
# the first) must resolve it through mirage_require_locked_artifact(), which
# is a configure error for any artifact missing from the lock file.

# Captured at include time (CMAKE_CURRENT_LIST_DIR points at cmake/ while the
# include() is processed; inside function bodies it would re-evaluate to the
# caller's directory). MIRAGE_DEPENDENCY_LOCK_DIR redirects the lock and the
# registered files to a fixture directory — used by the lock-gate regression
# test (tests/cmake/dependency_lock_test.cmake); normal configure never sets it.
if(DEFINED MIRAGE_DEPENDENCY_LOCK_DIR)
    set(_MIRAGE_PROJECT_ROOT "${MIRAGE_DEPENDENCY_LOCK_DIR}")
else()
    set(_MIRAGE_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

function(_mirage_dependency_lock out_json)
    set(lock_file "${_MIRAGE_PROJECT_ROOT}/dependencies.lock.json")
    if(NOT EXISTS "${lock_file}")
        message(FATAL_ERROR
            "dependencies.lock.json is missing at ${lock_file}. The pinned "
            "dependency lock is mandatory; restore the file before configuring.")
    endif()
    file(READ "${lock_file}" lock_json)
    set(${out_json} "${lock_json}" PARENT_SCOPE)
endfunction()

# string(JSON) wrapper that turns a missing member into a hard configure
# error, so a malformed lock entry cannot slip through as an empty value.
function(_mirage_json_field json out_var where)
    set(_keys ${ARGN})
    string(JSON _value ERROR_VARIABLE _err GET "${json}" ${_keys})
    if(_err)
        message(FATAL_ERROR
            "dependencies.lock.json ${where}: missing required member "
            "'${_keys}' (${_err}).")
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_mirage_sync_submodule dep_path dep_name)
    if(NOT MIRAGE_FETCH_DEPENDENCIES)
        message(FATAL_ERROR
            "Pinned dependency '${dep_name}' is not checked out at ${dep_path}. "
            "Run 'git submodule update --init --recursive' or configure with "
            "-DMIRAGE_FETCH_DEPENDENCIES=ON.")
    endif()
    message(STATUS "Mirage: fetching pinned dependency '${dep_name}' ...")
    execute_process(
        COMMAND git submodule update --init --recursive -- "${dep_path}"
        WORKING_DIRECTORY "${_MIRAGE_PROJECT_ROOT}"
        RESULT_VARIABLE git_result
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to sync pinned dependency '${dep_name}'; see git output above.")
    endif()
endfunction()

function(_mirage_verify_commit dep_name dep_dir expected_commit)
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${dep_dir}"
        OUTPUT_VARIABLE actual_commit
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT actual_commit STREQUAL expected_commit)
        message(FATAL_ERROR
            "Pinned dependency '${dep_name}' commit mismatch: dependencies.lock.json "
            "requires ${expected_commit} but the worktree has ${actual_commit}. "
            "Run 'git submodule update --init --recursive', or update the lock "
            "file deliberately through a dependency-upgrade change.")
    endif()
    message(STATUS "Mirage: pinned dependency '${dep_name}' verified at ${actual_commit}")
endfunction()

function(_mirage_verify_nested dep_name dep_dir nested_path expected_commit)
    execute_process(
        COMMAND git rev-parse "HEAD:${nested_path}"
        WORKING_DIRECTORY "${dep_dir}"
        OUTPUT_VARIABLE actual_commit
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT actual_commit STREQUAL expected_commit)
        message(FATAL_ERROR
            "Nested pin '${dep_name}:${nested_path}' mismatch: dependencies.lock.json "
            "requires ${expected_commit} but the parent repository records "
            "${actual_commit}.")
    endif()
    message(STATUS "Mirage: nested pin '${dep_name}:${nested_path}' verified at ${actual_commit}")
endfunction()

# Structural validation of the schema v2 sections (artifacts[] + frontend).
# Fails closed on any missing or malformed member so a hand-edited lock file
# cannot weaken the gate.
function(mirage_validate_lock_extensions lock_json)
    string(JSON artifact_count ERROR_VARIABLE err LENGTH "${lock_json}" "artifacts")
    if(err)
        message(FATAL_ERROR
            "dependencies.lock.json schema v2 requires an 'artifacts' array (${err}).")
    endif()
    if(artifact_count GREATER 0)
        math(EXPR artifact_last "${artifact_count} - 1")
        foreach(i RANGE 0 ${artifact_last})
            set(where "artifacts[${i}]")
            _mirage_json_field("${lock_json}" name "${where}" "artifacts" "${i}" "name")
            _mirage_json_field("${lock_json}" kind "${where}" "artifacts" "${i}" "kind")
            _mirage_json_field("${lock_json}" version "${where}" "artifacts" "${i}" "version")
            _mirage_json_field("${lock_json}" url_template "${where}" "artifacts" "${i}" "url_template")
            _mirage_json_field("${lock_json}" license "${where}" "artifacts" "${i}" "license")
            if(NOT kind STREQUAL "artifact")
                message(FATAL_ERROR
                    "dependencies.lock.json ${where}: unknown artifact kind '${kind}'.")
            endif()
            foreach(member name kind version url_template license)
                if("${${member}}" STREQUAL "")
                    message(FATAL_ERROR
                        "dependencies.lock.json ${where}: required member '${member}' is empty.")
                endif()
            endforeach()
            string(JSON platform_count ERROR_VARIABLE perr
                LENGTH "${lock_json}" "artifacts" "${i}" "platforms")
            if(perr)
                message(FATAL_ERROR
                    "dependencies.lock.json ${where}: missing required member 'platforms'.")
            endif()
            if(platform_count EQUAL 0)
                message(FATAL_ERROR
                    "dependencies.lock.json ${where}: 'platforms' must pin at least one platform.")
            endif()
            math(EXPR platform_last "${platform_count} - 1")
            foreach(p RANGE 0 ${platform_last})
                set(pwhere "${where}.platforms[${p}]")
                _mirage_json_field("${lock_json}" p_platform "${pwhere}"
                    "artifacts" "${i}" "platforms" "${p}" "platform")
                _mirage_json_field("${lock_json}" p_sha1 "${pwhere}"
                    "artifacts" "${i}" "platforms" "${p}" "sha1")
                _mirage_json_field("${lock_json}" p_size "${pwhere}"
                    "artifacts" "${i}" "platforms" "${p}" "size_bytes")
                string(LENGTH "${p_sha1}" p_sha1_length)
                if(p_sha1_length EQUAL 40 AND p_sha1 MATCHES "^[0-9a-f]+$")
                    message(STATUS
                        "Mirage: artifact pin '${name}' ${p_platform} "
                        "(sha1 ${p_sha1}, ${p_size} bytes)")
                else()
                    message(FATAL_ERROR
                        "dependencies.lock.json ${pwhere}: 'sha1' must be a lowercase "
                        "40-hex digest (got '${p_sha1}').")
                endif()
                if(NOT p_size MATCHES "^[0-9]+$")
                    message(FATAL_ERROR
                        "dependencies.lock.json ${pwhere}: 'size_bytes' must be a "
                        "non-negative integer (got '${p_size}').")
                endif()
            endforeach()
        endforeach()
    endif()

    string(JSON front_name ERROR_VARIABLE err GET "${lock_json}" "frontend" "name")
    if(err)
        message(FATAL_ERROR
            "dependencies.lock.json schema v2 requires a 'frontend' npm-tree "
            "registration (${err}).")
    endif()
    _mirage_json_field("${lock_json}" lock_rel "frontend" "frontend" "lockfile")
    _mirage_json_field("${lock_json}" lock_hash "frontend" "frontend" "lockfile_sha256")
    string(LENGTH "${lock_hash}" lock_hash_length)
    if(NOT lock_hash_length EQUAL 64 OR NOT lock_hash MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "dependencies.lock.json frontend: 'lockfile_sha256' must be a lowercase "
            "64-hex sha256 digest (got '${lock_hash}').")
    endif()
    set(lock_file "${_MIRAGE_PROJECT_ROOT}/${lock_rel}")
    if(NOT EXISTS "${lock_file}")
        message(FATAL_ERROR
            "dependencies.lock.json frontend '${front_name}': registered lockfile "
            "'${lock_rel}' does not exist.")
    endif()
    file(READ "${lock_file}" lockfile_bytes)
    # The digest is defined over LF-normalized content: git stores the lockfile
    # with LF, while a Windows working tree may check it out with CRLF
    # (core.autocrlf). Normalizing makes the registered hash checkout-agnostic.
    string(REPLACE "\r\n" "\n" lockfile_normalized "${lockfile_bytes}")
    string(SHA256 actual_hash "${lockfile_normalized}")
    if(NOT actual_hash STREQUAL lock_hash)
        message(FATAL_ERROR
            "dependencies.lock.json frontend '${front_name}': ${lock_rel} drifted from "
            "the repository-level registration (declared ${lock_hash}, actual "
            "${actual_hash} over LF-normalized content). Verify the dependency change "
            "deliberately and update 'frontend.lockfile_sha256' in the same change; "
            "CI installs with 'npm ci' to keep the tree aligned with the lockfile.")
    endif()
    message(STATUS
        "Mirage: frontend npm tree '${front_name}' locked at ${lock_hash}")
endfunction()

# Consumption gate for locked binary artifacts (DEC-006 decision 6): a product
# target that consumes a pinned artifact resolves it through this function.
# Unregistered names are a configure error, so an unlocked binary cannot enter
# the build silently. Sets <out_prefix>_VERSION, <out_prefix>_URL and
# <out_prefix>_UPSTREAM_INDEX in the caller's scope.
function(mirage_require_locked_artifact name out_prefix)
    _mirage_dependency_lock(lock_json)
    string(JSON artifact_count ERROR_VARIABLE err LENGTH "${lock_json}" "artifacts")
    set(found_index)
    if(NOT err AND artifact_count GREATER 0)
        math(EXPR artifact_last "${artifact_count} - 1")
        foreach(i RANGE 0 ${artifact_last})
            string(JSON entry_name ERROR_VARIABLE entry_err
                GET "${lock_json}" "artifacts" "${i}" "name")
            if(NOT entry_err AND entry_name STREQUAL name)
                set(found_index "${i}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT DEFINED found_index)
        message(FATAL_ERROR
            "Artifact '${name}' is not registered in dependencies.lock.json. Register "
            "it through a deliberate lock change (version, url, per-platform sha1 and "
            "size, license) before a product target may consume it — DEC-006 decision "
            "6: no unlocked binary enters the default build.")
    endif()
    _mirage_json_field("${lock_json}" version "${name}" "artifacts" "${found_index}" "version")
    _mirage_json_field("${lock_json}" url "${name}" "artifacts" "${found_index}" "url_template")
    _mirage_json_field("${lock_json}" index_url "${name}" "artifacts" "${found_index}" "upstream_index")
    set(${out_prefix}_VERSION "${version}" PARENT_SCOPE)
    set(${out_prefix}_URL "${url}" PARENT_SCOPE)
    set(${out_prefix}_UPSTREAM_INDEX "${index_url}" PARENT_SCOPE)
    message(STATUS "Mirage: artifact '${name}' resolved from the lock at ${version}")
endfunction()

# The lock file declares schema_version 2 (submodule dependencies[], binary
# artifacts[], frontend npm-tree registration). Raising the version requires
# updating this module in the same change.
function(mirage_require_lock_schema_v2 lock_json)
    string(JSON schema_version ERROR_VARIABLE schema_err GET "${lock_json}" "schema_version")
    if(schema_err OR NOT schema_version EQUAL 2)
        message(FATAL_ERROR
            "dependencies.lock.json schema_version must be 2 (found "
            "'${schema_version}'). Update cmake/MirageDependencies.cmake together "
            "with the lock file.")
    endif()
endfunction()

function(mirage_resolve_pinned_dependencies)
    _mirage_dependency_lock(lock_json)
    mirage_require_lock_schema_v2("${lock_json}")
    mirage_validate_lock_extensions("${lock_json}")
    string(JSON dep_count LENGTH "${lock_json}" "dependencies")
    if(dep_count EQUAL 0)
        message(FATAL_ERROR "dependencies.lock.json declares no dependencies.")
    endif()
    math(EXPR last_index "${dep_count} - 1")
    foreach(index RANGE 0 ${last_index})
        string(JSON dep_name GET "${lock_json}" "dependencies" "${index}" "name")
        string(JSON dep_path GET "${lock_json}" "dependencies" "${index}" "path")
        string(JSON dep_commit GET "${lock_json}" "dependencies" "${index}" "commit")
        set(dep_dir "${_MIRAGE_PROJECT_ROOT}/${dep_path}")
        if(NOT EXISTS "${dep_dir}/.git")
            _mirage_sync_submodule("${dep_path}" "${dep_name}")
        endif()
        if(MIRAGE_VERIFY_DEPENDENCIES)
            _mirage_verify_commit("${dep_name}" "${dep_dir}" "${dep_commit}")
            string(JSON nested_count ERROR_VARIABLE nested_error
                LENGTH "${lock_json}" "dependencies" "${index}" "nested_pins")
            if(NOT nested_error)
                if(nested_count GREATER 0)
                    math(EXPR nested_last "${nested_count} - 1")
                    foreach(nested_index RANGE 0 ${nested_last})
                        string(JSON nested_path GET "${lock_json}"
                            "dependencies" "${index}" "nested_pins" "${nested_index}" "path")
                        string(JSON nested_commit GET "${lock_json}"
                            "dependencies" "${index}" "nested_pins" "${nested_index}" "commit")
                        _mirage_verify_nested("${dep_name}" "${dep_dir}"
                            "${nested_path}" "${nested_commit}")
                    endforeach()
                endif()
            endif()
        endif()
    endforeach()
endfunction()
