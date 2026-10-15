# One platform backend per binary (design doc section 10): the OS-specific
# translation unit is selected here so unsupported platforms fail configure
# instead of compiling nothing and silently linking an empty surface.
#
# The backend implements the desktop provider interfaces (adapter depends on
# the desktop core interfaces, DEC-008); the product call direction stays
# runtime -> desktop interfaces.
add_library(mirage_platform STATIC common/src/frontend_process.cpp)
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
        linux/src/frontend_activation.cpp
        linux/src/platform_linux.cpp
        linux/src/linux_desktop_environment.cpp
        linux/src/linux_file_actions.cpp
        linux/src/credential_store.cpp
        linux/src/portal_screen_backend.cpp
        linux/src/portal_input.cpp
        linux/src/portal_authorization.cpp
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
        option(MIRAGE_ENABLE_X11_DRAG_HELPERS "Resolve same-client X11 drag helper windows" ON)
        if(MIRAGE_ENABLE_X11_DRAG_HELPERS AND X11_XRes_FOUND)
            target_link_libraries(mirage_platform PRIVATE X11::XRes)
            target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_XRES=1)
            set_property(TARGET mirage_platform PROPERTY MIRAGE_X11_DRAG_HELPERS_AVAILABLE TRUE)
            message(STATUS "Linux X11 drag helper ownership enabled (XRes)")
        else()
            message(STATUS "Linux X11 drag helpers use strict rejection: XRes absent or disabled")
        endif()
        message(STATUS "Linux X11 backend enabled (X11 ${X11_VERSION})")
    else()
        target_sources(mirage_platform PRIVATE linux/src/x11_backend_stub.cpp)
        message(STATUS "Linux X11 backend disabled: X11/Xtst/Xrandr dev packages not found")
    endif()
    # The X11 overlay carrier (M5-09, DEC-029) rides the same optionality
    # discipline and additionally needs the shape extension (Xext): absent
    # dev packages compile the stub and open_overlay_carrier() returns null.
    if(X11_FOUND AND X11_Xtst_FOUND AND X11_Xrandr_FOUND AND X11_Xshape_FOUND)
        target_sources(mirage_platform PRIVATE linux/src/overlay_backend.cpp)
        target_link_libraries(mirage_platform PRIVATE ${X11_Xext_LIB})
        target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_XOVERLAY=1)
        message(STATUS "Linux X11 overlay carrier enabled")
    else()
        target_sources(mirage_platform PRIVATE linux/src/overlay_backend_stub.cpp)
        message(STATUS "Linux X11 overlay carrier disabled: X11/Xshape dev packages not found")
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
        find_package(PNG QUIET)
        if(PNG_FOUND)
            target_link_libraries(mirage_platform PRIVATE PNG::PNG)
            target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_PORTAL=1)
            set_property(TARGET mirage_platform PROPERTY MIRAGE_PORTAL_AVAILABLE TRUE)
            # GLOBAL: tests/ links the same target (portal_stream_retention_test);
            # a directory-scoped import is invisible to the sibling directory.
            pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET GLOBAL libpipewire-0.3)
            set(MIRAGE_PIPEWIRE_SDK_ROOT "" CACHE PATH "Optional extracted distribution PipeWire development SDK")
            if(PIPEWIRE_FOUND)
                target_link_libraries(mirage_platform PRIVATE PkgConfig::PIPEWIRE)
            elseif(MIRAGE_PIPEWIRE_SDK_ROOT)
                find_path(MIRAGE_PIPEWIRE_HEADERS pipewire/pipewire.h
                    PATHS "${MIRAGE_PIPEWIRE_SDK_ROOT}/usr/include/pipewire-0.3" NO_DEFAULT_PATH REQUIRED)
                find_path(MIRAGE_SPA_HEADERS spa/param/video/format-utils.h
                    PATHS "${MIRAGE_PIPEWIRE_SDK_ROOT}/usr/include/spa-0.2" NO_DEFAULT_PATH REQUIRED)
                find_library(MIRAGE_PIPEWIRE_LIBRARY NAMES pipewire-0.3 libpipewire-0.3.so.0 REQUIRED)
                target_include_directories(mirage_platform SYSTEM PRIVATE ${MIRAGE_PIPEWIRE_HEADERS} ${MIRAGE_SPA_HEADERS})
                target_link_libraries(mirage_platform PRIVATE ${MIRAGE_PIPEWIRE_LIBRARY})
            endif()
            if(PIPEWIRE_FOUND OR MIRAGE_PIPEWIRE_SDK_ROOT)
                target_sources(mirage_platform PRIVATE linux/src/portal_pipewire_capture.cpp)
                target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_PIPEWIRE=1)
                set_property(TARGET mirage_platform PROPERTY MIRAGE_PIPEWIRE_AVAILABLE TRUE)
                message(STATUS "Linux Wayland ScreenCast/PipeWire capture enabled")
            else()
                # A portal build without PipeWire has no reusable authorized
                # observation; portal_screen_backend fails capture closed rather
                # than degrading silently, but the product must not ship that way
                # by accident. Make the gap loud at configure time.
                message(WARNING
                    "Linux Wayland ScreenCast/PipeWire capture DISABLED: libpipewire-0.3 dev "
                    "files not found and MIRAGE_PIPEWIRE_SDK_ROOT is empty. Wayland desktop "
                    "observation will fail closed (unsupported_capability) instead of sharing "
                    "the screen. Install libpipewire-0.3 and libspa-0.2 development files or "
                    "point MIRAGE_PIPEWIRE_SDK_ROOT at an extracted SDK, then reconfigure.")
            endif()
        endif()
        target_sources(mirage_platform PRIVATE
            linux/src/application_backend.cpp
            linux/src/notification_backend.cpp
            linux/src/tray_backend.cpp
        )
        target_link_libraries(mirage_platform PRIVATE PkgConfig::GIO_UNIX)
        target_compile_definitions(mirage_platform PRIVATE MIRAGE_LINUX_GIO=1)
        message(STATUS "Linux application/notification/tray frontends enabled (${GIO_UNIX_VERSION})")
    else()
        target_sources(mirage_platform PRIVATE
            linux/src/application_backend_stub.cpp
            linux/src/notification_backend_stub.cpp
            linux/src/tray_backend_stub.cpp
        )
        message(STATUS "Linux application/notification/tray frontends disabled: gio-unix-2.0 dev package not found")
    endif()
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_include_directories(mirage_platform
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/windows/include>
    )
    target_sources(mirage_platform PRIVATE
        windows/src/frontend_activation.cpp
        windows/src/platform_windows.cpp
        windows/src/credential_store.cpp
        windows/src/windows_desktop_environment.cpp
        # The Win32 frontend (M4-01), the UIA accessibility frontend
        # (M4-02), the application frontend (M4-04), the notification
        # frontend (M4-05), the overlay frontend (M5-09, DEC-029) and the
        # tray frontend (M5-10, DEC-030) have no stub variant: user32 /
        # gdi32 / ole32 / oleaut32 / shell32 are part of every Windows SDK,
        # so the only honest failure mode is a probe failure at open() time
        # (accessors null, fail closed) — never a compile-time absence.
        # shell32 carries the known-folder API the Start Menu discovery
        # resolves its roots through and the Shell_NotifyIcon carrier of the
        # notification and tray frontends; the shell-link COM object needs
        # no import library (its GUIDs are defined in the frontend, the
        # interfaces resolve via __uuidof).
        windows/src/win32_backend.cpp
        # The tiered screen capture engine (M7-10, DEC-086): DXGI Desktop
        # Duplication -> Windows.Graphics.Capture -> GDI BitBlt. Its platform
        # link face is d3d11 (device + the WinRT Direct3D interop entry),
        # dxgi (factory / output / duplication) and windowsapp (the WinRT
        # activation / HSTRING umbrella, which carries combase's import
        # surface — the SDK ships no standalone combase.lib) — system
        # components of every Windows SDK, so like the rest of the Windows
        # frontends the only honest failure mode is a per-call refusal,
        # never a compile-time absence.
        windows/src/screen_capture_backend.cpp
        windows/src/uia_backend.cpp
        windows/src/application_backend.cpp
        windows/src/notification_backend.cpp
        windows/src/overlay_backend.cpp
        windows/src/tray_backend.cpp
    )
    target_link_libraries(mirage_platform PRIVATE user32 gdi32 ole32 oleaut32 shell32 advapi32
                                                  d3d11 dxgi windowsapp)
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
else()
    message(FATAL_ERROR
        "Mirage targets Linux and Windows only (design doc section 10); "
        "${CMAKE_SYSTEM_NAME} is out of scope.")
endif()
