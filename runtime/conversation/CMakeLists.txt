# Product-owned conversation projection, independent of execution/platform APIs.
add_library(mirage_conversation STATIC src/conversation.cpp)
add_library(Mirage::conversation ALIAS mirage_conversation)
target_include_directories(mirage_conversation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(mirage_conversation PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_conversation)
target_link_libraries(mirage_conversation PRIVATE Mira::core)
