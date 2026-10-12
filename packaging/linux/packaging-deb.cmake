# DEC-037: native UI + CLI + service + tray; no Chromium payload.
# DEC-073: a product package must contain the validated ScreenCast transport,
# rather than silently shipping Screenshot-only fallback on native Wayland.
get_target_property(_mirage_package_pipewire mirage_platform MIRAGE_PIPEWIRE_AVAILABLE)
if(NOT _mirage_package_pipewire)
    message(FATAL_ERROR "Product packaging requires the PipeWire development SDK (libpipewire-0.3-dev/libspa-0.2-dev); users only need runtime dependencies")
endif()
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "mirage")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Mirage: Linux/Windows desktop host for the Mira agent runtime")
set(CPACK_PACKAGE_VENDOR "Mirage maintainers")
set(CPACK_PACKAGE_CONTACT "Mirage maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
# Auto dependency detection via dpkg-shlibdeps (dpkg-dev): libc, libstdc++
# and the gio family the platform backend was built with.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# GLFW loads these libraries at runtime; shlibdeps cannot infer them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgl1, libglx0, libxcursor1, libxi6, libxinerama1")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "zenity | kdialog, fonts-noto-cjk, xdg-desktop-portal, xdg-desktop-portal-gnome | xdg-desktop-portal-kde | xdg-desktop-portal-wlr | xdg-desktop-portal-gtk")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Mirage maintainers")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_LIST_DIR}/postinst;${CMAKE_CURRENT_LIST_DIR}/prerm")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

install(TARGETS mirage RUNTIME DESTINATION bin)
install(TARGETS mirage-service RUNTIME DESTINATION bin)
install(TARGETS mirage-tray RUNTIME DESTINATION bin)
if(NOT TARGET mirage-native)
    message(FATAL_ERROR "Packaging requires -DMIRAGE_ENABLE_NATIVE_FRONTEND=ON (DEC-037)")
endif()
install(TARGETS mirage-native RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/apps/native/assets/mira.png" "${CMAKE_SOURCE_DIR}/apps/native/assets/mira-ui.png"
    "${CMAKE_SOURCE_DIR}/third_party/eui-neo/assets/Font Awesome 7 Free-Solid-900.otf"
    "${CMAKE_BINARY_DIR}/apps/native/font-assets/NotoSansSC-Regular.otf"
    "${CMAKE_SOURCE_DIR}/apps/native/assets/fonts/OFL-NotoSansSC.txt" DESTINATION bin/assets)
configure_file("${CMAKE_SOURCE_DIR}/apps/native/org.mirage.native.installed.desktop.in"
    "${CMAKE_BINARY_DIR}/org.mirage.native.installed.desktop" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/org.mirage.native.installed.desktop"
    DESTINATION share/applications RENAME org.mirage.native.desktop)
install(FILES "${CMAKE_SOURCE_DIR}/apps/native/assets/mira.png"
    DESTINATION share/icons/hicolor/256x256/apps RENAME mirage.png)

if(NOT MIRAGE_BUNDLE_VISION)
    message(FATAL_ERROR "Product packaging requires MIRAGE_BUNDLE_VISION=ON (DEC-068)")
endif()
install(DIRECTORY "${MIRAGE_VISION_BUNDLE_DIR}/" DESTINATION share/mirage/vision/ppocrv5-mobile-v1)
include(CPack)
