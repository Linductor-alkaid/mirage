# The desktop product shell (M5-02, DEC-006 frozen route): a CEF browser
# process that hosts the product UI and connects to mirage-service over
# Local IPC. The IPC contract is the only coupling face — the UI is built in
# ui/ (its own npm workspace) and served into the shell as static assets, so
# the renderer never joins the C++ target graph.
#
# Consumption gate (DEC-006 decision 6, shell-binary-locking section 2.1):
# the shell is opt-in because it downloads the locked ~360-675 MB CEF
# artifact; when enabled it binds the artifact through the lock (configure
# fails for an unregistered pin or a digest mismatch), so no unlocked binary
# can enter any build. CI enables the shell explicitly on the Windows job;
# the Linux shell build joins when the CI matrix gains the X11 build
# dependencies (recorded in the M5-02 plan verification round).

option(MIRAGE_ENABLE_DESKTOP_SHELL
    "Build the CEF desktop product shell (mirage-desktop; downloads the locked CEF artifact)"
    OFF)
if(NOT MIRAGE_ENABLE_DESKTOP_SHELL)
    # Deliberately no shell target in the default build (the pre-M5 stance
    # documented in apps/CMakeLists.txt).
    return()
endif()

include("${CMAKE_SOURCE_DIR}/cmake/MirageCef.cmake")

# The built UI assets the shell serves (vite build output). The shell loads
# them through its own mirage://app scheme, so a missing build is a configure
# error with a concrete instruction, not a blank window at runtime.
set(MIRAGE_UI_APP_DIST "${CMAKE_SOURCE_DIR}/ui/app/dist" CACHE PATH
    "Built UI application assets (ui/app vite build output) served by the desktop shell")
if(NOT EXISTS "${MIRAGE_UI_APP_DIST}/index.html")
    message(FATAL_ERROR
        "The desktop shell serves the built UI from MIRAGE_UI_APP_DIST "
        "(${MIRAGE_UI_APP_DIST}), but index.html is missing there. Build the UI "
        "first: npm run build in ui/ (see ui/README.md).")
endif()

mirage_acquire_locked_cef(CEF_ROOT)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CEF_ROOT}/cmake")
# CEF 152 Windows clients build as a DLL loaded by the distribution's
# bootstrap.exe (the sandbox boundary); that path assumes the classic /MT
# runtime, which conflicts with the Mirage toolchain (/MD, DEC-017). The flag
# is a documented cache knob read inside find_package(CEF) — it must be set
# BEFORE it, so the CEF targets (wrapper and shell) emit /MDd //MD and share
# one CRT with mirage_ipc and the pinned executor. Sandbox enablement is
# unaffected (M5-01 formal policy: sandbox on, GPU on, no kill switch).
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING
        "Runtime flag for CEF targets (MD: match the Mirage toolchain)")
endif()
find_package(CEF REQUIRED)

ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

# Shared target configuration: sources, compile feature, include path, the
# compile-time asset root, and the CEF linkage. The executor is consumed
# exactly like the dev bridge and the runtime service: SYSTEM headers and a
# file-path link, so the pinned fabric's flag set never leaks into Mirage
# targets.
function(_mirage_desktop_shell_configure target)
    target_sources(${target} PRIVATE
        src/bridge_core.cpp
        src/desktop_app.cpp
        src/desktop_client.cpp
        src/desktop_renderer_app.cpp
        src/shell_main.cpp
        src/shell_session.cpp
        src/ui_scheme_handler.cpp
        $<$<PLATFORM_ID:Windows>:src/shell_win.cpp>
        $<$<NOT:$<PLATFORM_ID:Windows>>:src/main.cpp>
    )
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CEF_ROOT}")
    target_compile_definitions(${target} PRIVATE
        MIRAGE_UI_APP_DIST="${MIRAGE_UI_APP_DIST}"
        MIRAGE_VERSION="${PROJECT_VERSION}")
    mirage_enable_warnings(${target})
    target_link_libraries(${target} PRIVATE
        libcef_lib
        libcef_dll_wrapper
        Mirage::ipc
        $<TARGET_FILE:executor::executor>)
    target_include_directories(${target} SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include")
endfunction()

# Deploys the CEF payload next to the shell target. The copy steps use
# $<CONFIG> generator expressions because CEF's own COPY_FILES assumes a
# single-config CMAKE_BUILD_TYPE, which the VS multi-config generator (the
# Mirage Windows gate) does not provide.
function(_mirage_desktop_shell_deploy target)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_bin_dir "$<IF:$<CONFIG:Debug>,${CEF_ROOT}/Debug,${CEF_ROOT}/Release>")
        set(_payload "")
        foreach(_file ${CEF_BINARY_FILES})
            list(APPEND _payload "${_bin_dir}/${_file}")
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_payload}
                $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_ROOT}/Resources/" $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
            VERBATIM)
        # bootstrap.exe renamed to the shell's product process name: it loads
        # the mirage-desktop DLL and owns the Windows sandbox setup.
        COPY_SINGLE_FILE(${target} "${_bin_dir}/bootstrap.exe"
            "$<TARGET_FILE_DIR:${target}>/mirage-desktop.exe")
    endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows (CEF 152, USE_SANDBOX=ON default): the shell is a SHARED
    # library loaded by bootstrap.exe (the same layout cefsimple builds).
    add_library(mirage-desktop SHARED)
    _mirage_desktop_shell_configure(mirage-desktop)
    SET_LIBRARY_TARGET_PROPERTIES(mirage-desktop)
    _mirage_desktop_shell_deploy(mirage-desktop)
else()
    # Linux: classic single executable; the chrome-sandbox setuid setup stays
    # with packaging (M5-11), mirroring the cefsimple layout.
    find_package(X11 REQUIRED)
    SET_CEF_TARGET_OUT_DIR()
    add_executable(mirage-desktop)
    _mirage_desktop_shell_configure(mirage-desktop)
    SET_EXECUTABLE_TARGET_PROPERTIES(mirage-desktop)
    set_target_properties(mirage-desktop PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
        RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")
    _mirage_desktop_shell_deploy(mirage-desktop)
    COPY_FILES(mirage-desktop "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
    COPY_FILES(mirage-desktop "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
    target_link_libraries(mirage-desktop PRIVATE ${X11_LIBRARIES})
    target_include_directories(mirage-desktop PRIVATE ${X11_INCLUDE_DIR})
endif()
