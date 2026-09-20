add_library(mirage_desktop STATIC
    environment/src/desktop_environment.cpp
    environment/src/visual_snapshot.cpp
    observation/src/observation_assembler.cpp
)
add_library(Mirage::desktop ALIAS mirage_desktop)

target_include_directories(mirage_desktop
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/environment/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/filesystem/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/observation/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/process/include>
)
target_compile_features(mirage_desktop PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_desktop)
