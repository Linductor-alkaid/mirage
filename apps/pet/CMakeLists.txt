add_library(mirage_pet_loader STATIC loader.cpp)
target_compile_features(mirage_pet_loader PUBLIC cxx_std_20)
target_include_directories(mirage_pet_loader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# kairo via the shared consumer face (DEC-096).
target_link_libraries(mirage_pet_loader PUBLIC Mirage::pet_state
    PRIVATE Mirage::kairo_consumer)
mirage_enable_warnings(mirage_pet_loader)

if(MIRAGE_BUILD_TESTS)
    add_executable(pet_frame_presentation_test "${CMAKE_SOURCE_DIR}/tests/runtime/pet_frame_presentation_test.cpp")
    target_link_libraries(pet_frame_presentation_test PRIVATE Mirage::pet_state)
    mirage_enable_warnings(pet_frame_presentation_test)
    add_test(NAME pet_frame_presentation_test COMMAND pet_frame_presentation_test)
    set_tests_properties(pet_frame_presentation_test PROPERTIES LABELS "unit;runtime;pet" TIMEOUT 10)
    add_executable(pet_resource_loader_test "${CMAKE_SOURCE_DIR}/tests/runtime/pet_resource_loader_test.cpp")
    target_link_libraries(pet_resource_loader_test PRIVATE mirage_pet_loader
        Mirage::kairo_consumer)
    mirage_enable_warnings(pet_resource_loader_test)
    add_test(NAME pet_resource_loader_test COMMAND pet_resource_loader_test)
    set_tests_properties(pet_resource_loader_test PROPERTIES LABELS "unit;runtime;pet" TIMEOUT 30)
endif()
if(NOT MIRAGE_ENABLE_PET_PREVIEW AND NOT MIRAGE_ENABLE_PET_ASSET_TESTS)
    return()
endif()

if(MIRAGE_ENABLE_PET_PREVIEW AND NOT MIRAGE_ENABLE_NATIVE_FRONTEND)
    message(FATAL_ERROR "mira_pet preview requires MIRAGE_ENABLE_NATIVE_FRONTEND=ON")
endif()
set(MIRAGE_PET_ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets" CACHE PATH
    "Approved v5 WebP source directory")
if(NOT IS_DIRECTORY "${MIRAGE_PET_ASSET_SOURCE_DIR}")
    message(FATAL_ERROR "mira_pet preview needs MIRAGE_PET_ASSET_SOURCE_DIR")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MIRAGE_WEBPDEMUX REQUIRED IMPORTED_TARGET libwebpdemux)

set(mirage_pet_manifest "${CMAKE_SOURCE_DIR}/docs/compatibility/data/mira-pet-playback-v5-20261013.json")
file(READ "${mirage_pet_manifest}" mirage_pet_manifest_json)
string(JSON mirage_pet_clip_count LENGTH "${mirage_pet_manifest_json}" clips)
if(NOT mirage_pet_clip_count EQUAL 9)
    message(FATAL_ERROR "mira_pet v5 manifest must contain nine clips")
endif()
set(mirage_pet_assets "")
foreach(index RANGE 0 8)
    string(JSON filename GET "${mirage_pet_manifest_json}" clips ${index} file)
    string(JSON expected_sha GET "${mirage_pet_manifest_json}" clips ${index} sha256)
    set(source_file "${MIRAGE_PET_ASSET_SOURCE_DIR}/${filename}")
    if(NOT EXISTS "${source_file}" OR IS_SYMLINK "${source_file}")
        message(FATAL_ERROR "mira_pet v5 asset missing or symlink: ${filename}")
    endif()
    file(SHA256 "${source_file}" actual_sha)
    if(NOT actual_sha STREQUAL expected_sha)
        message(FATAL_ERROR "mira_pet v5 asset digest mismatch: ${filename}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_file}")
    list(APPEND mirage_pet_assets "${source_file}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mirage_pet_manifest}")

add_library(mirage_pet_frames STATIC frames.cpp)
target_compile_features(mirage_pet_frames PUBLIC cxx_std_20)
target_include_directories(mirage_pet_frames PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mirage_pet_frames PUBLIC Mirage::pet_state PkgConfig::MIRAGE_WEBPDEMUX PRIVATE Mira::core)
set(mirage_pet_digest_header "${CMAKE_CURRENT_BINARY_DIR}/asset_digests.hpp")
file(WRITE "${mirage_pet_digest_header}" "#pragma once\n#include <array>\n#include <string_view>\n#include <utility>\ninline constexpr std::array<std::pair<std::string_view, std::string_view>, 9> pet_asset_digests{{\n")
foreach(index RANGE 0 8)
    string(JSON filename GET "${mirage_pet_manifest_json}" clips ${index} file)
    string(JSON expected_sha GET "${mirage_pet_manifest_json}" clips ${index} sha256)
    file(APPEND "${mirage_pet_digest_header}" "{\"${filename}\", \"${expected_sha}\"},\n")
endforeach()
file(APPEND "${mirage_pet_digest_header}" "}};\n")
target_include_directories(mirage_pet_frames PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
mirage_enable_warnings(mirage_pet_frames)

if(MIRAGE_BUILD_TESTS)
    add_executable(pet_frames_test "${CMAKE_SOURCE_DIR}/tests/runtime/pet_frames_test.cpp")
    target_link_libraries(pet_frames_test PRIVATE mirage_pet_frames mirage_pet_loader
        Mirage::kairo_consumer)
    target_compile_features(pet_frames_test PRIVATE cxx_std_20)
    target_compile_definitions(pet_frames_test PRIVATE
        MIRAGE_PET_TEST_ASSETS="${MIRAGE_PET_ASSET_SOURCE_DIR}")
    mirage_enable_warnings(pet_frames_test)
    add_test(NAME pet_frames_test COMMAND pet_frames_test)
    set_tests_properties(pet_frames_test PROPERTIES LABELS "unit;runtime;pet" TIMEOUT 60)
endif()

if(NOT MIRAGE_ENABLE_PET_PREVIEW)
    return()
endif()

add_executable(mira_pet entry.cpp app.cpp bridge.cpp
    "${CMAKE_SOURCE_DIR}/platform/pet/src/pet_window.cpp")
eui_neo_configure_app(mira_pet)
get_property(mirage_pet_eui_entry GLOBAL PROPERTY EUI_NEO_APP_MAIN_SOURCE)
set_source_files_properties("${mirage_pet_eui_entry}" TARGET_DIRECTORY mira_pet
    PROPERTIES COMPILE_DEFINITIONS "main=mirage_pet_eui_main")
target_compile_features(mira_pet PRIVATE cxx_std_20)
target_include_directories(mira_pet PRIVATE
    "${CMAKE_SOURCE_DIR}/platform/pet/include")
target_include_directories(mira_pet SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/eui-neo"
    "${CMAKE_SOURCE_DIR}/third_party/eui-neo/include")
target_link_libraries(mira_pet PRIVATE mirage_pet_frames mirage_pet_loader Mirage::ipc
    Mirage::platform Mirage::persistence Mirage::kairo_consumer)
if(NOT WIN32)
    target_link_libraries(mira_pet PRIVATE ${X11_Xext_LIB})
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mira_pet PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
endif()
mirage_enable_warnings(mira_pet)
set_target_properties(mira_pet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/pet")
add_custom_target(mira_pet_assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mira_pet>/assets/pet"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mirage_pet_assets} "$<TARGET_FILE_DIR:mira_pet>/assets/pet"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/apps/native/font-assets/NotoSansSC-Regular.otf"
        "$<TARGET_FILE_DIR:mira_pet>/assets/NotoSansSC-Regular.otf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/apps/native/assets/fonts/OFL-NotoSansSC.txt"
        "$<TARGET_FILE_DIR:mira_pet>/assets/OFL-NotoSansSC.txt"
    VERBATIM)
add_dependencies(mira_pet mira_pet_assets)
