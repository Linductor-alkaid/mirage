option(MIRAGE_ENABLE_NATIVE_FRONTEND "Build the EUI-NEO native conversation preview" OFF)
if(NOT MIRAGE_ENABLE_NATIVE_FRONTEND)
    return()
endif()

# Offline, pinned sources; EUI carries no product scheduling responsibilities.
set(EUI_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_USER_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_TEST_FIXTURES OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_TRAY OFF CACHE BOOL "" FORCE)
set(EUI_DEPS_MODE bundled CACHE STRING "" FORCE)
set(EUI_WINDOW_BACKEND glfw CACHE STRING "" FORCE)
set(EUI_RENDER_BACKEND opengl CACHE STRING "" FORCE)
include("${CMAKE_SOURCE_DIR}/cmake/MirageEuiGlfw.cmake")
mirage_provide_eui_glfw()
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/eui-neo"
                 "${CMAKE_BINARY_DIR}/third_party/eui-neo" EXCLUDE_FROM_ALL)
# EUI-20261003-002: dev platform.cpp catches filesystem errors but its
# Release flags disable exceptions. Enable them through target properties.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eui_neo PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
endif()

add_executable(mirage-native app.cpp chat_model.cpp window_controls.cpp runtime_bridge.cpp)
# Replace EUI's example executable icon with the imported Mira artwork.
if(WIN32)
    set_property(GLOBAL PROPERTY EUI_NEO_APP_ICON_RESOURCE "")
    set(mirage_icon_path "${CMAKE_CURRENT_SOURCE_DIR}/assets/mira.ico")
    string(REPLACE "\\" "/" mirage_icon_path "${mirage_icon_path}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mirage_icon.rc"
        "IDI_MIRAGE_ICON ICON \"${mirage_icon_path}\"\n")
    target_sources(mirage-native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mirage_icon.rc")
endif()
eui_neo_configure_app(mirage-native)
# Copy by target directory for single- and multi-config generators; no ~/mira dependency.
add_custom_target(mirage-native-assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mirage-native>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/mira.png"
        "$<TARGET_FILE_DIR:mirage-native>/assets/mira.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/mira-ui.png"
        "$<TARGET_FILE_DIR:mirage-native>/assets/mira-ui.png"
    VERBATIM)
add_dependencies(mirage-native mirage-native-assets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mirage-native PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
endif()
target_compile_features(mirage-native PRIVATE cxx_std_20)
set_target_properties(mirage-native PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/native")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # GNOME docks resolve the application icon through its desktop entry;
    # the file identity and StartupWMClass match DslAppConfig::appId.
    file(GENERATE
        OUTPUT "$<TARGET_FILE_DIR:mirage-native>/org.mirage.native.desktop"
        INPUT "${CMAKE_CURRENT_SOURCE_DIR}/org.mirage.native.desktop.in")
    add_custom_target(mirage-native-register-desktop
        COMMAND ${CMAKE_COMMAND}
            "-DMIRAGE_DESKTOP_ENTRY=$<TARGET_FILE_DIR:mirage-native>/org.mirage.native.desktop"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/register_desktop.cmake"
        DEPENDS mirage-native
        VERBATIM)
endif()
# Public upstream component headers contain their own implementation; don't
# apply Mirage's Werror policy to that code. Our three sources retain warnings.
target_include_directories(mirage-native SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/eui-neo"
    "${CMAKE_SOURCE_DIR}/third_party/eui-neo/include")
mirage_enable_warnings(mirage-native)

target_link_libraries(mirage-native PRIVATE Mirage::ipc Mirage::persistence executor)
target_include_directories(mirage-native SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include")
