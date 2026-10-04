# EUI-20261003-001: dev's IME gate records CRLF hashes, but git stores LF.
# Use EUI's documented externally supplied GLFW target, compiled directly
# from its already-fixed bundled source. No source patches/copies or workers.
function(mirage_provide_eui_glfw)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        set(source "${CMAKE_SOURCE_DIR}/third_party/eui-neo/3rd/glfw/src/x11_window.c")
        file(READ "${source}" content)
        string(REPLACE "\r\n" "\n" content "${content}")
        string(SHA256 hash "${content}")
        if(NOT hash STREQUAL "8ce625aa965d6da401ce3f4992fdb53ac4ecd34bc00b22bbaeffab41f72b0dae")
            message(FATAL_ERROR "EUI-20261003-001: reviewed GLFW IME source changed; re-audit before upgrading")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    endif()
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/eui-neo/3rd/glfw"
                     "${CMAKE_BINARY_DIR}/third_party/eui-glfw" EXCLUDE_FROM_ALL)
endfunction()
