# Public-header boundary check (RULE-01): the public include trees of
# desktop, runtime and platform must not contain pinned third-party
# includes (mira / mirador / executor). integration/ is the designated
# boundary layer and is deliberately not checked. Gated on the
# mirage-boundary-check custom target so dependency builds are never
# touched.
file(GLOB_RECURSE MIRAGE_PUBLIC_HEADERS
    "${ROOT_DIR}/desktop/*/include/*.hpp"
    "${ROOT_DIR}/runtime/*/include/*.hpp"
    "${ROOT_DIR}/platform/include/*.hpp"
    "${ROOT_DIR}/platform/*/include/*.hpp"
)
set(MIRAGE_BOUNDARY_VIOLATIONS "")
foreach(FILE_PATH IN LISTS MIRAGE_PUBLIC_HEADERS)
    file(READ "${FILE_PATH}" MIRAGE_HEADER_CONTENT)
    if(MIRAGE_HEADER_CONTENT MATCHES
       "#[ \t]*include[ \t]*[<\"](mira|mirador|executor)/")
        list(APPEND MIRAGE_BOUNDARY_VIOLATIONS "${FILE_PATH}")
    endif()
endforeach()
if(MIRAGE_BOUNDARY_VIOLATIONS)
    foreach(FILE_PATH IN LISTS MIRAGE_BOUNDARY_VIOLATIONS)
        message(SEND_ERROR
            "Public header contains pinned third-party includes: ${FILE_PATH}")
    endforeach()
    message(FATAL_ERROR
        "Public header boundary check failed "
        "(${MIRAGE_BOUNDARY_VIOLATIONS} violation(s))")
else()
    list(LENGTH MIRAGE_PUBLIC_HEADERS MIRAGE_HEADER_COUNT)
    message(STATUS
        "Public header boundary check passed "
        "(0 violations in ${MIRAGE_HEADER_COUNT} headers)")
endif()
