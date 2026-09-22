# MinGW-w64 x86_64 cross toolchain: compile-gate for the Windows backend on
# Linux hosts (M4-01, DEC-017 decision 1-2). This gate proves "can build with
# the specified toolchain" only — runtime evidence needs a real Windows
# session (CI windows job or the maintainer machine).
#
# Toolchain location: `$MIRAGE_MINGW_PREFIX` (a user-prefix extraction of the
# distro mingw-w64 packages, e.g. via `apt-get download` + `dpkg -x`, DEC-015
# decision 4 discipline for root-less environments) or `$PATH` for a system
# installation. Both `bin/` and `usr/bin/` layouts are probed.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(DEFINED ENV{MIRAGE_MINGW_PREFIX} AND NOT "$ENV{MIRAGE_MINGW_PREFIX}" STREQUAL "")
    set(_MIRAGE_MINGW_PREFIX "$ENV{MIRAGE_MINGW_PREFIX}")
    foreach(_dir "${_MIRAGE_MINGW_PREFIX}/bin" "${_MIRAGE_MINGW_PREFIX}/usr/bin")
        if(EXISTS "${_dir}")
            list(APPEND CMAKE_PROGRAM_PATH "${_dir}")
        endif()
    endforeach()
    unset(_MIRAGE_MINGW_PREFIX)
else()
    message(STATUS
        "MIRAGE_MINGW_PREFIX is not set; searching $PATH for the MinGW-w64 "
        "cross toolchain (x86_64-w64-mingw32-*).")
endif()

find_program(CMAKE_C_COMPILER NAMES x86_64-w64-mingw32-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES x86_64-w64-mingw32-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES x86_64-w64-mingw32-windres REQUIRED)
find_program(CMAKE_AR NAMES x86_64-w64-mingw32-gcc-ar x86_64-w64-mingw32-ar REQUIRED)
find_program(CMAKE_RANLIB NAMES x86_64-w64-mingw32-gcc-ranlib x86_64-w64-mingw32-ranlib REQUIRED)
find_program(CMAKE_STRIP NAMES x86_64-w64-mingw32-strip REQUIRED)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
