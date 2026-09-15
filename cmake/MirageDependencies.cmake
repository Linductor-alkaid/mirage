# Resolve and verify the pinned third_party dependencies declared in
# dependencies.lock.json (docs/project/project-standards.md section 9.1).
#
# Two operating modes:
#   MIRAGE_FETCH_DEPENDENCIES=ON  (default) sync missing submodules at
#                                 configure time, then verify commits.
#   MIRAGE_FETCH_DEPENDENCIES=OFF verify-only path for offline / CI builds;
#                                 a missing checkout is a hard error.
#
# The top-level entries (mira, mirador) are verified against the submodule
# worktree. Nested pins (executor, mbedtls, googletest, ...) are verified
# against the gitlink recorded by their parent repository, so a parent
# checkout that silently moved a nested submodule fails configure.

# Captured at include time (CMAKE_CURRENT_LIST_DIR points at cmake/ while the
# include() is processed; inside function bodies it would re-evaluate to the
# caller's directory).
set(_MIRAGE_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

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

function(mirage_resolve_pinned_dependencies)
    _mirage_dependency_lock(lock_json)
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
