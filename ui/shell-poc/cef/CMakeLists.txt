# Mirage shell PoC：CEF 壳承载（DEC-006 M3-06，非产品代码）。
#
# 独立 CMake 工程，位于 Mirage 主构建图之外（DEC-006 第 6 条：二进制来源未进入
# 锁定机制前不得进入默认构建）。用法：
#   cmake -B build -S . -DCEF_ROOT=<解包的 CEF binary distribution 路径>
#   cmake --build build --target poc_cef
# 产物在 build/poc_cef/ 目录（CEF 资源经 COPY_FILES 就位）。
cmake_minimum_required(VERSION 3.21)

project(mirage_shell_poc_cef LANGUAGES C CXX)

set(CEF_ROOT "" CACHE PATH "Path to an extracted CEF binary distribution")
if(NOT CEF_ROOT)
  message(FATAL_ERROR "需要 -DCEF_ROOT=<解包的 CEF binary distribution 路径>")
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# mira/json.hpp：Mirage 的 JSON 编解码路由（AGENTS.md 能力路由），仅 PoC 使用。
# mira 的 JSON 实现位于 src/support/json.cpp（自包含，仅依赖 mira 头文件），
# 直接编入 PoC 目标，不把 pinned mira 全量引入本独立工程。
set(MIRAGE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(MIRA_INCLUDE_DIR "${MIRAGE_REPO_ROOT}/third_party/mira/include")
set(MIRA_JSON_SRC "${MIRAGE_REPO_ROOT}/third_party/mira/src/support/json.cpp")
if(NOT EXISTS "${MIRA_INCLUDE_DIR}/mira/json.hpp" OR NOT EXISTS "${MIRA_JSON_SRC}")
  message(FATAL_ERROR "未找到 mira/json.hpp 或其实现：${MIRA_INCLUDE_DIR}")
endif()

ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

find_package(X11 REQUIRED)

add_executable(poc_cef
  src/main.cpp
  src/poc_app_browser.cpp
  src/poc_app_renderer.cpp
  "${MIRA_JSON_SRC}"
)
SET_EXECUTABLE_TARGET_PROPERTIES(poc_cef)
target_compile_features(poc_cef PRIVATE cxx_std_20)
# mira json.cpp 使用异常；CEF 目标旗标为 -fno-exceptions，对单文件后置覆盖。
set_source_files_properties("${MIRA_JSON_SRC}" PROPERTIES COMPILE_OPTIONS "-fexceptions")
# GCC 13 对 mira::JsonValue（variant 内嵌 vector）移动构造报 maybe-uninitialized
# 误报（CEF 标志含 -Werror），PoC 工具侧显式抑制；不影响产品构建。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(poc_cef PRIVATE -Wno-maybe-uninitialized)
endif()
target_compile_options(poc_cef PRIVATE -g)
add_dependencies(poc_cef libcef_dll_wrapper)
target_link_libraries(poc_cef libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS}
  ${X11_LIBRARIES})
target_include_directories(poc_cef PRIVATE "${CEF_ROOT}" "${MIRA_INCLUDE_DIR}")

# 输出与 CEF 资源布局：可执行文件与 libcef.so / pak 资源同目录（rpath $ORIGIN）。
SET_CEF_TARGET_OUT_DIR()
set_target_properties(poc_cef PROPERTIES
  INSTALL_RPATH "$ORIGIN"
  BUILD_WITH_INSTALL_RPATH TRUE
  RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")
COPY_FILES(poc_cef "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(poc_cef "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
