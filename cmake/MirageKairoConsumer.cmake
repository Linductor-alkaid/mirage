# One consumption face for the kairo fabric delivered by pinned mira
# (REUSE-08/REUSE-13): process-level Executor owners and kairo::comm
# consumers include+link through this target instead of hardcoding the
# nested pin's include path. The consumption form mirrors the discipline
# recorded in runtime/service: headers as SYSTEM (the pinned library's own
# warning hygiene stays out of Mirage's -Werror build) and the library
# linked by file path so kairo's non-SYSTEM interface include dirs do not
# leak into consumer targets.
if(NOT TARGET kairo::kairo)
    message(FATAL_ERROR "MirageKairoConsumer expects the pinned kairo targets from third_party/mira")
endif()
add_library(Mirage_kairo_consumer INTERFACE)
add_library(Mirage::kairo_consumer ALIAS Mirage_kairo_consumer)
target_include_directories(Mirage_kairo_consumer SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/third_party/mira/third_party/kairo/include")
target_link_libraries(Mirage_kairo_consumer INTERFACE $<TARGET_FILE:kairo::kairo>)
