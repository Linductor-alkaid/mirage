# EUI-20261003-001: dev's IME gate records CRLF hashes, but git stores LF.
# Use EUI's documented externally supplied GLFW target, compiled directly
# from its pinned bundled source, including authorized XIM repair PR#88
# (DEC-041 / EUI-20261005-008). No configure-time patches or workers.
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
        if(NOT hash STREQUAL "a4b7e67319996fc50b3bc0ae6943e6141a0b6b400e3c4f3f7279979dafd02258")
            message(FATAL_ERROR "EUI-20261003-001: reviewed GLFW IME source changed; re-audit before upgrading")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    endif()
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/eui-neo/3rd/glfw"
                     "${CMAKE_BINARY_DIR}/third_party/eui-glfw" EXCLUDE_FROM_ALL)
endfunction()
