# Locked CEF artifact acquisition (M5-02; DEC-006 decision 6 and
# docs/supply-chain/shell-binary-locking.md section 2.1).
#
# The shell is the first product target consuming a pinned binary artifact.
# Consumption discipline:
#   - the pin (version, URL template, per-platform size + sha1) comes from
#     dependencies.lock.json via mirage_require_locked_artifact_platform();
#     an unregistered artifact or platform is a configure error;
#   - a cached archive that fails the digest check is a hard configure error
#     ("configure 失败于摘要不匹配"), never a silent re-download;
#   - a cold cache downloads once (URL from the lock) with
#     file(DOWNLOAD ... EXPECTED_HASH SHA1=...), so a server-side digest
#     mismatch fails the configure as well;
#   - the archive is unpacked once into the cache; the extracted distribution
#     layout is validated before CEF_ROOT is handed out.
#
# The cache lives outside any build tree so it survives reconfiguration and
# can be pre-seeded (offline machines, CI cache). No artifact binary ever
# enters the repository.

# Acquires the pinned CEF binary distribution for the host platform and sets
# <out_root> to the extracted distribution root.
#
#   mirage_acquire_locked_cef(cef_root
#       CACHE_DIR "${CMAKE_SOURCE_DIR}/build/artifact-cache")
function(mirage_acquire_locked_cef out_root)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "CACHE_DIR" "")
    if(NOT ARG_CACHE_DIR)
        set(ARG_CACHE_DIR "${CMAKE_SOURCE_DIR}/build/artifact-cache")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_platform "windows64")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_platform "linux64")
    else()
        message(FATAL_ERROR
            "The desktop shell has no locked CEF artifact for platform "
            "'${CMAKE_SYSTEM_NAME}'. Extend dependencies.lock.json deliberately "
            "before consuming one.")
    endif()

    mirage_require_locked_artifact_platform("cef" "${_platform}" cef_pin)
    set(_version "${cef_pin_VERSION}")
    set(_url "${cef_pin_URL}")
    set(_sha1 "${cef_pin_SHA1}")
    set(_size "${cef_pin_SIZE_BYTES}")

    file(MAKE_DIRECTORY "${ARG_CACHE_DIR}")
    get_filename_component(_archive_name "${_url}" NAME)
    # The CDN URL percent-encodes '+' as %2B; the canonical distribution name
    # (and the tar's internal top-level directory) use the literal character.
    string(REPLACE "%2B" "+" _archive_name "${_archive_name}")
    set(_archive "${ARG_CACHE_DIR}/${_archive_name}")

    if(EXISTS "${_archive}")
        file(SHA1 "${_archive}" _actual_sha1)
        file(SIZE "${_archive}" _actual_size)
        if(NOT _actual_sha1 STREQUAL _sha1)
            message(FATAL_ERROR
                "Cached CEF archive '${_archive}' fails the locked digest check "
                "(expected sha1 ${_sha1}, actual ${_actual_sha1}). Delete the cached "
                "archive only after verifying its origin; the lock digest is the "
                "fail-closed boundary (DEC-006 decision 6).")
        endif()
        if(NOT _actual_size EQUAL _size)
            message(FATAL_ERROR
                "Cached CEF archive '${_archive}' fails the locked size check "
                "(expected ${_size} bytes, actual ${_actual_size}) while its sha1 "
                "matched. The lock entry is inconsistent; fix dependencies.lock.json "
                "or repin the artifact deliberately.")
        endif()
        message(STATUS "Mirage: cached CEF archive verified against the lock (${_sha1})")
    else()
        message(STATUS "Mirage: downloading locked CEF artifact (${_platform}, "
            "${_size} bytes, sha1 ${_sha1})")
        file(DOWNLOAD "${_url}" "${_archive}"
            EXPECTED_HASH SHA1=${_sha1}
            STATUS _download_status)
        list(GET _download_status 0 _download_error)
        if(NOT _download_error EQUAL 0)
            list(GET _download_status 1 _download_message)
            file(REMOVE "${_archive}")
            message(FATAL_ERROR
                "Downloading the locked CEF artifact failed: ${_download_message}. "
                "The archive URL comes from dependencies.lock.json; a digest "
                "mismatch or unreachable source is a hard error (fail closed).")
        endif()
        file(SIZE "${_archive}" _actual_size)
        if(NOT _actual_size EQUAL _size)
            file(REMOVE "${_archive}")
            message(FATAL_ERROR
                "Downloaded CEF archive has ${_actual_size} bytes but the lock pins "
                "${_size}; refusing to consume it (fail closed).")
        endif()
    endif()

    # The tar's internal top-level directory is authoritative for the
    # distribution root (canonical name cef_binary_<version>_<platform>); do
    # not guess it from the archive filename.
    set(_dist_dir "${ARG_CACHE_DIR}/cef_binary_${_version}_${_platform}")
    if(NOT EXISTS "${_dist_dir}/include/cef_app.h"
            OR NOT EXISTS "${_dist_dir}/cmake/FindCEF.cmake")
        message(STATUS "Mirage: unpacking the locked CEF distribution")
        file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${ARG_CACHE_DIR}")
    endif()
    if(NOT EXISTS "${_dist_dir}/include/cef_app.h"
            OR NOT EXISTS "${_dist_dir}/cmake/FindCEF.cmake")
        message(FATAL_ERROR
            "Unpacked CEF distribution at '${_dist_dir}' is missing the expected "
            "layout (include/cef_app.h, cmake/FindCEF.cmake). The archive's digest "
            "matched the lock but its content does not — treat as a compromised "
            "artifact, delete the cache and escalate.")
    endif()

    set(${out_root} "${_dist_dir}" PARENT_SCOPE)
endfunction()
