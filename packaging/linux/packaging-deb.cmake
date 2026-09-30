# CPack DEB packaging (M5-11, DEC-031): the .deb ships whatever this build
# tree produced — mirage (CLI), mirage-service, mirage-tray always; the CEF
# desktop shell and its chrome-sandbox helper only when the shell was built
# (MIRAGE_ENABLE_DESKTOP_SHELL). Maintainer scripts live in this directory;
# the chrome-sandbox setuid bits are applied by postinst at install time
# (root), never at build time.
#
# Enabled with -DMIRAGE_ENABLE_PACKAGING=ON; `cpack -G DEB` from the build
# directory then produces the package. Install/upgrade/uninstall on a real
# system requires root and is verified on the release/CI path (M5-11
# verification record; local sandboxes keep no-new-privileges).

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
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Mirage maintainers")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_LIST_DIR}/postinst;${CMAKE_CURRENT_LIST_DIR}/prerm")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

install(TARGETS mirage RUNTIME DESTINATION bin)
install(TARGETS mirage-service RUNTIME DESTINATION bin)
install(TARGETS mirage-tray RUNTIME DESTINATION bin)
if(TARGET mirage-desktop)
    install(TARGETS mirage-desktop RUNTIME DESTINATION bin)
    # CEF chrome-sandbox helper ships with the shell payload (M5-01 sandbox
    # policy); setuid bits are applied by postinst. Full CEF payload
    # (libcef, resources) packaging is a release-machine step: this build
    # tree must be configured with the shell ON and the payload copied next
    # to the binary by the shell target's own deploy rules.
    if(DEFINED MIRAGE_CEF_SANDBOX_FILE AND EXISTS "${MIRAGE_CEF_SANDBOX_FILE}")
        install(FILES "${MIRAGE_CEF_SANDBOX_FILE}" DESTINATION lib/mirage)
    endif()
endif()

include(CPack)
