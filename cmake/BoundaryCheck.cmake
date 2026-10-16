# Public-header boundary check (RULE-01, DEC-096): the public include trees
# of desktop, runtime and platform must not contain pinned third-party
# includes (mira / mirador / kairo) — neither directly nor transitively
# through the mirage/ include trees of desktop, runtime, platform and
# integration. integration/ is the designated boundary layer and may include
# pinned headers itself, but a public header that reaches one through it
# exposes the pinned surface and is flagged. The paths listed in
# MIRAGE_BOUNDARY_RECORDED_SEAMS are the recorded exceptions (DEC-096): the
# Runtime Service / Mira Host public configuration surfaces carry integration
# adapter types pending the pinned-free migration; NEW transitive arrivals
# fail the check. Gated on the mirage-boundary-check custom target so
# dependency builds are never touched.

file(GLOB_RECURSE MIRAGE_PUBLIC_HEADERS
    "${ROOT_DIR}/desktop/*/include/*.hpp"
    "${ROOT_DIR}/runtime/*/include/*.hpp"
    "${ROOT_DIR}/platform/include/*.hpp"
    "${ROOT_DIR}/platform/*/include/*.hpp"
)

# Include roots a mirage/... include may resolve against. Integration roots
# resolve too — that transitive path is exactly what this check polices.
set(MIRAGE_INCLUDE_ROOTS "${ROOT_DIR}/platform/include")
file(GLOB MIRAGE_DESKTOP_ROOTS "${ROOT_DIR}/desktop/*/include")
file(GLOB MIRAGE_RUNTIME_ROOTS "${ROOT_DIR}/runtime/*/include")
file(GLOB MIRAGE_PLATFORM_ROOTS "${ROOT_DIR}/platform/*/include")
list(APPEND MIRAGE_INCLUDE_ROOTS
    ${MIRAGE_DESKTOP_ROOTS}
    ${MIRAGE_RUNTIME_ROOTS}
    ${MIRAGE_PLATFORM_ROOTS}
    "${ROOT_DIR}/integration/mira/include"
    "${ROOT_DIR}/integration/mirador/include")

# Recorded transitive seams (DEC-096): public headers allowed to reach pinned
# includes through integration adapter surfaces. Absolute paths.
set(MIRAGE_BOUNDARY_RECORDED_SEAMS
    "${ROOT_DIR}/runtime/service/include/mirage/runtime/runtime_service.hpp"
    "${ROOT_DIR}/runtime/mira_host/include/mirage/runtime/mira_host.hpp")

set(MIRAGE_BOUNDARY_VIOLATIONS "")
set(MIRAGE_RECORDED_SEAM_HITS "")
set(MIRAGE_PINNED_RX "^(mira|mirador|kairo)/")

foreach(FILE_PATH IN LISTS MIRAGE_PUBLIC_HEADERS)
    set(MIRAGE_QUEUE "${FILE_PATH}")
    set(MIRAGE_VISITED "${FILE_PATH}")
    set(MIRAGE_HIT "")
    while(MIRAGE_QUEUE)
        list(GET MIRAGE_QUEUE 0 MIRAGE_CURRENT)
        list(REMOVE_AT MIRAGE_QUEUE 0)
        file(READ "${MIRAGE_CURRENT}" MIRAGE_CONTENT)
        string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
            MIRAGE_INCLUDES "${MIRAGE_CONTENT}")
        foreach(RAW IN LISTS MIRAGE_INCLUDES)
            string(REGEX REPLACE "^.*[<\"]([^>\"]+)[>\"].*$" "\\1" MIRAGE_INC "${RAW}")
            if(MIRAGE_INC MATCHES "${MIRAGE_PINNED_RX}")
                if(NOT MIRAGE_HIT)
                    set(MIRAGE_HIT
                        "${FILE_PATH} (via ${MIRAGE_CURRENT} -> ${MIRAGE_INC})")
                endif()
                break()
            endif()
            string(FIND "${MIRAGE_INC}" "mirage/" MIRAGE_MIRAGE_POS)
            if(NOT MIRAGE_MIRAGE_POS EQUAL 0)
                continue() # std/system headers: not policed
            endif()
            foreach(ROOT IN LISTS MIRAGE_INCLUDE_ROOTS)
                if(EXISTS "${ROOT}/${MIRAGE_INC}")
                    get_filename_component(MIRAGE_RESOLVED "${ROOT}/${MIRAGE_INC}" ABSOLUTE)
                    list(FIND MIRAGE_VISITED "${MIRAGE_RESOLVED}" MIRAGE_SEEN)
                    if(MIRAGE_SEEN EQUAL -1)
                        list(APPEND MIRAGE_VISITED "${MIRAGE_RESOLVED}")
                        list(APPEND MIRAGE_QUEUE "${MIRAGE_RESOLVED}")
                    endif()
                    break()
                endif()
            endforeach()
        endforeach()
    endwhile()
    if(MIRAGE_HIT)
        list(FIND MIRAGE_BOUNDARY_RECORDED_SEAMS "${FILE_PATH}" MIRAGE_RECORDED)
        if(NOT MIRAGE_RECORDED EQUAL -1)
            list(APPEND MIRAGE_RECORDED_SEAM_HITS "${MIRAGE_HIT}")
        else()
            list(APPEND MIRAGE_BOUNDARY_VIOLATIONS "${MIRAGE_HIT}")
        endif()
    endif()
endforeach()

foreach(HIT IN LISTS MIRAGE_BOUNDARY_VIOLATIONS)
    message(SEND_ERROR
        "Public header reaches pinned third-party includes: ${HIT}")
endforeach()
if(MIRAGE_BOUNDARY_VIOLATIONS)
    message(FATAL_ERROR
        "Public header boundary check failed "
        "(${MIRAGE_BOUNDARY_VIOLATIONS} violation(s); recorded seams live in "
        "cmake/BoundaryCheck.cmake, policy in DEC-096)")
else()
    list(LENGTH MIRAGE_PUBLIC_HEADERS MIRAGE_HEADER_COUNT)
    list(LENGTH MIRAGE_RECORDED_SEAM_HITS MIRAGE_SEAM_COUNT)
    message(STATUS
        "Public header boundary check passed "
        "(0 violations in ${MIRAGE_HEADER_COUNT} headers; "
        "${MIRAGE_SEAM_COUNT} recorded seam arrival(s) per DEC-096)")
    foreach(HIT IN LISTS MIRAGE_RECORDED_SEAM_HITS)
        message(STATUS "Recorded seam (DEC-096): ${HIT}")
    endforeach()
endif()
