add_library(mirage_pet_state STATIC src/status.cpp src/animation.cpp)
add_library(Mirage::pet_state ALIAS mirage_pet_state)
target_include_directories(mirage_pet_state PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(mirage_pet_state PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_pet_state)
