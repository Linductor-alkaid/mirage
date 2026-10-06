# DEC-045: the product tray embeds RuntimeService; no separate Executor.
add_executable(mirage-tray main.cpp)
target_compile_features(mirage-tray PUBLIC cxx_std_20)
mirage_enable_warnings(mirage-tray)
target_link_libraries(mirage-tray PRIVATE mirage_service_host)
