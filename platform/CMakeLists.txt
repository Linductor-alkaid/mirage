# One platform backend per binary (design doc section 10): the OS-specific
# translation unit is selected here so unsupported platforms fail configure
# instead of compiling nothing and silently linking an empty surface.
#
# The backend implements the desktop provider interfaces (adapter depends on
# the desktop core interfaces, DEC-008); the product call direction stays
# runtime -> desktop interfaces.
add_library(mirage_platform STATIC)
add_library(Mirage::platform ALIAS mirage_platform)

target_include_directories(mirage_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_platform PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_platform)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(mirage_platform
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/linux/include>
    )
    target_sources(mirage_platform PRIVATE
        linux/src/platform_linux.cpp
        linux/src/linux_desktop_environment.cpp
    )
    # The X11/XWayland frontend (M2-02) is optional so a machine without the
    # X11 dev packages still builds the platform library; the backend then
    # reports no window/screen/input capability (fail closed, DEC-015).
    find_package(X11 QUIET)
    if(X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND)
        target_sources(mirage_platform PRIVATE linux/src/x11_backend.cpp)
        target_link_libraries(mirage_platform PRIVATE ${X11_X11_LIB} ${X11_Xtst_LIB}
                                                      ${X11_Xrandr_LIB})
        target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_X11=1)
        message(STATUS "Linux X11 backend enabled (X11 ${X11_VERSION})")
    else()
        target_sources(mirage_platform PRIVATE linux/src/x11_backend_stub.cpp)
        message(STATUS "Linux X11 backend disabled: X11/Xtst/Xrandr dev packages not found")
    endif()
    # The AT-SPI2 accessibility frontend (M2-03) follows the same optionality
    # discipline (DEC-015): libatspi pulls glib, both stay PRIVATE to the
    # platform target and out of every public header (RULE-01).
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(ATSPI QUIET IMPORTED_TARGET atspi-2 gobject-2.0)
    endif()
    if(ATSPI_FOUND)
        target_sources(mirage_platform PRIVATE linux/src/atspi_backend.cpp)
        # IMPORTED_TARGET carries the transitive link deps; gobject-2.0 is added
        # explicitly because the atspi-2.pc Requires omits it (upstream
        # packaging miss, noted in the M2-03 verification record).
        target_link_libraries(mirage_platform PRIVATE PkgConfig::ATSPI)
        target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_ATSPI=1)
        message(STATUS "Linux AT-SPI2 backend enabled (${ATSPI_VERSION})")
    else()
        target_sources(mirage_platform PRIVATE linux/src/atspi_backend_stub.cpp)
        message(STATUS "Linux AT-SPI2 backend disabled: atspi-2 dev package not found")
    endif()
    # The Application / Notification frontends (M2-05) ride the same glib
    # family as AT-SPI2 (DEC-015 decision 3): GDesktopAppInfo for desktop
    # discovery and launch, GDBus for the freedesktop Notifications service.
    # Same optionality discipline: missing gio-unix dev packages compile the
    # stubs and the accessors report no capability (fail closed).
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GIO_UNIX QUIET IMPORTED_TARGET gio-unix-2.0)
    endif()
    if(GIO_UNIX_FOUND)
        target_sources(mirage_platform PRIVATE
            linux/src/application_backend.cpp
            linux/src/notification_backend.cpp
        )
        target_link_libraries(mirage_platform PRIVATE PkgConfig::GIO_UNIX)
        target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_GIO=1)
        message(STATUS "Linux application/notification frontends enabled (${GIO_UNIX_VERSION})")
    else()
        target_sources(mirage_platform PRIVATE
            linux/src/application_backend_stub.cpp
            linux/src/notification_backend_stub.cpp
        )
        message(STATUS "Linux application/notification frontends disabled: gio-unix-2.0 dev package not found")
    endif()
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_include_directories(mirage_platform
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/windows/include>
    )
    target_sources(mirage_platform PRIVATE
        windows/src/platform_windows.cpp
        windows/src/windows_desktop_environment.cpp
        # The Win32 frontend (M4-01), the UIA accessibility frontend
        # (M4-02) and the application frontend (M4-04) have no stub
        # variant: user32 / gdi32 / ole32 / oleaut32 / shell32 are part of
        # every Windows SDK, so the only honest failure mode is a probe
        # failure at open() time (accessors null, fail closed) — never a
        # compile-time absence. shell32 carries the known-folder API the
        # Start Menu discovery resolves its roots through; the shell-link
        # COM object needs no import library (its GUIDs are defined in the
        # frontend, the interfaces resolve via __uuidof).
        windows/src/win32_backend.cpp
        windows/src/uia_backend.cpp
        windows/src/application_backend.cpp
    )
    target_link_libraries(mirage_platform PRIVATE user32 gdi32 ole32 oleaut32 shell32)
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
else()
    message(FATAL_ERROR
        "Mirage targets Linux and Windows only (design doc section 10); "
        "${CMAKE_SYSTEM_NAME} is out of scope.")
endif()
