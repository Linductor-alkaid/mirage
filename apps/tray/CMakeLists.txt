# The tray process form (M5-10, DEC-030): a notification-area /
# status-indicator process that talks to mirage-service exclusively over
# Local IPC (EXEC-02) through the pinned Executor fabric (consumed exactly
# like the desktop shell and the dev bridge: SYSTEM headers + file-path
# link, one Executor owner per process, EXEC-01). The platform carrier is
# probed at startup — a session without an indicator facility reports and
# exits (capability honesty, DEC-030).
add_executable(mirage-tray
    main.cpp
    tray_core.cpp
)

target_include_directories(mirage-tray
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(mirage-tray PUBLIC cxx_std_20)
mirage_enable_warnings(mirage-tray)

target_link_libraries(mirage-tray
    PRIVATE
        Mirage::ipc
        Mirage::desktop
        Mirage::platform
        $<TARGET_FILE:executor::executor>
)
target_include_directories(mirage-tray SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/executor/include"
)
