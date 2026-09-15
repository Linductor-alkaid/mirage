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
    target_link_libraries(mirage_platform PUBLIC Mirage::desktop)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(mirage_platform PRIVATE windows/src/platform_windows.cpp)
else()
    message(FATAL_ERROR
        "Mirage targets Linux and Windows only (design doc section 10); "
        "${CMAKE_SYSTEM_NAME} is out of scope.")
endif()
