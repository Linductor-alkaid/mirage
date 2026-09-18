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
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(mirage_platform PRIVATE windows/src/platform_windows.cpp)
else()
    message(FATAL_ERROR
        "Mirage targets Linux and Windows only (design doc section 10); "
        "${CMAKE_SYSTEM_NAME} is out of scope.")
endif()
